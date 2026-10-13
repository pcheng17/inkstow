//! Native disk watching. Like the Qt app, watches cover the vault folders, the
//! selected note, and the nearest existing ancestor of a note whose folder
//! disappeared. Every event only requests a full `sync_with_disk` and rescan,
//! so events must be timely but not precise; Save still compares disk bytes.
use crate::Session;
use notify::event::{AccessKind, AccessMode, EventKind, ModifyKind};
use std::collections::{HashMap, HashSet};
use std::fs::{self, Metadata};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

#[derive(Debug)]
pub enum WatchError {
    /// The path disappeared before it could be watched; an ancestor reports its return.
    Vanished,
    /// The OS limit on watches was reached.
    Limit,
    Other(String),
}

/// Registers non-recursive watches; injectable so reconciliation is testable.
pub trait Backend: Send {
    fn watch(&mut self, path: &Path) -> Result<(), WatchError>;
    fn unwatch(&mut self, path: &Path);
    /// Paths reported created, removed or renamed since the last call. Their
    /// watches are re-registered even when a reused inode hides the change.
    fn take_moved(&mut self) -> Vec<PathBuf>;
}

/// Paths to watch, most important first so a watch limit drops vault folders
/// before the selected note. Like the Qt app, a missing note folder or vault
/// folder is replaced by its nearest existing ancestor, which reports its return.
pub fn targets(session: &Session, folders: &[String]) -> Vec<PathBuf> {
    let Some(vault) = session.vault() else {
        return vec![];
    };
    let absolute = |relative: &str| {
        relative
            .split('/')
            .fold(vault.root().to_path_buf(), |path, part| path.join(part))
    };
    let mut targets = vec![];
    if let Some(note) = session.path().map(absolute) {
        let folder = note.parent().and_then(existing_dir);
        targets.push(note);
        targets.extend(folder);
    }
    targets.extend(existing_dir(vault.root()));
    targets.extend(folders.iter().map(|folder| absolute(folder)));
    targets
}

fn existing_dir(mut path: &Path) -> Option<PathBuf> {
    while !fs::symlink_metadata(path).is_ok_and(|m| m.is_dir()) {
        path = path.parent()?;
    }
    Some(path.to_path_buf())
}

/// Distinguishes a replaced or recreated path, whose old watch the OS dropped.
type Identity = (u64, u64);
#[cfg(unix)]
fn id(metadata: &Metadata) -> Identity {
    use std::os::unix::fs::MetadataExt;
    (metadata.dev(), metadata.ino())
}
#[cfg(not(unix))]
fn id(metadata: &Metadata) -> Identity {
    metadata
        .created()
        .ok()
        .and_then(|time| time.duration_since(std::time::UNIX_EPOCH).ok())
        .map_or((0, 0), |d| (d.as_secs(), d.subsec_nanos().into()))
}
fn identity(path: &Path) -> Option<Identity> {
    let metadata = fs::symlink_metadata(path).ok()?;
    (!metadata.file_type().is_symlink()).then(|| id(&metadata))
}

pub struct DiskWatch {
    backend: Option<Box<dyn Backend>>,
    watched: HashMap<PathBuf, Identity>,
    complete: bool,
}
impl DiskWatch {
    /// Without a backend nothing is watched and `complete` stays false.
    pub fn new(backend: Option<Box<dyn Backend>>) -> Self {
        Self {
            backend,
            watched: HashMap::new(),
            complete: false,
        }
    }
    /// Whether every existing target is watched. When false, the UI must poll.
    pub fn complete(&self) -> bool {
        self.complete
    }
    /// Re-registers replaced or recreated paths and drops stale watches. Run
    /// after every rescan, including when the note itself has disappeared.
    pub fn reconcile(&mut self, targets: &[PathBuf]) -> bool {
        let Some(backend) = self.backend.as_mut() else {
            self.complete = false;
            return false;
        };
        let wanted: HashSet<_> = targets.iter().collect();
        let moved: HashSet<_> = backend.take_moved().into_iter().collect();
        self.watched.retain(|path, old| {
            let keep =
                wanted.contains(path) && !moved.contains(path) && identity(path) == Some(*old);
            if !keep {
                backend.unwatch(path);
            }
            keep
        });
        let mut complete = true;
        for path in targets {
            if self.watched.contains_key(path) {
                continue;
            }
            // Read the identity first: a replacement racing the watch then
            // mismatches on the next reconcile instead of going unnoticed.
            let Some(current) = identity(path) else {
                continue;
            };
            match backend.watch(path) {
                Ok(()) => {
                    self.watched.insert(path.clone(), current);
                }
                Err(WatchError::Vanished) => (),
                // Further watches would fail too; stop instead of retrying each.
                Err(WatchError::Limit) => {
                    complete = false;
                    break;
                }
                Err(WatchError::Other(_)) => complete = false,
            }
        }
        self.complete = complete;
        complete
    }
}

/// Coalesces bursts of events like the Qt app's 150 ms sync timer, but also
/// fires during a continuous burst so changes are not deferred indefinitely.
#[derive(Debug)]
pub struct Debounce {
    quiet: Duration,
    longest: Duration,
    first: Option<Instant>,
    last: Option<Instant>,
}
impl Debounce {
    pub fn new(quiet: Duration, longest: Duration) -> Self {
        Self {
            quiet,
            longest,
            first: None,
            last: None,
        }
    }
    pub fn event(&mut self, now: Instant) {
        self.first.get_or_insert(now);
        self.last = Some(now);
    }
    pub fn deadline(&self) -> Option<Instant> {
        Some((self.last? + self.quiet).min(self.first? + self.longest))
    }
    /// Returns true once per burst, when its deadline has passed.
    pub fn fire(&mut self, now: Instant) -> bool {
        let due = self.deadline().is_some_and(|deadline| deadline <= now);
        if due {
            self.first = None;
            self.last = None;
        }
        due
    }
}

/// Calls `on_change` in event order on a single thread, after each burst.
fn forward(events: Receiver<()>, mut debounce: Debounce, on_change: impl Fn()) {
    loop {
        let received = match debounce.deadline() {
            None => events.recv().map_err(|_| RecvTimeoutError::Disconnected),
            Some(deadline) => {
                events.recv_timeout(deadline.saturating_duration_since(Instant::now()))
            }
        };
        match received {
            Ok(()) => debounce.event(Instant::now()),
            Err(RecvTimeoutError::Timeout) => (),
            Err(RecvTimeoutError::Disconnected) => return,
        }
        if debounce.fire(Instant::now()) {
            on_change();
        }
    }
}

/// Reading notes and listing folders produce access events on some platforms;
/// requesting a rescan for them would make every rescan trigger the next.
fn relevant(kind: &EventKind) -> bool {
    match kind {
        EventKind::Access(access) => *access == AccessKind::Close(AccessMode::Write),
        _ => true,
    }
}
fn moves(kind: &EventKind) -> bool {
    matches!(
        kind,
        EventKind::Create(_) | EventKind::Remove(_) | EventKind::Modify(ModifyKind::Name(_))
    )
}

struct Native {
    watcher: notify::RecommendedWatcher,
    moved: Arc<Mutex<Vec<PathBuf>>>,
}
impl Backend for Native {
    fn watch(&mut self, path: &Path) -> Result<(), WatchError> {
        use notify::{ErrorKind, Watcher};
        self.watcher
            .watch(path, notify::RecursiveMode::NonRecursive)
            .map_err(|error| match error.kind {
                ErrorKind::PathNotFound => WatchError::Vanished,
                ErrorKind::Io(ref e) if e.kind() == std::io::ErrorKind::NotFound => {
                    WatchError::Vanished
                }
                ErrorKind::MaxFilesWatch => WatchError::Limit,
                _ => WatchError::Other(error.to_string()),
            })
    }
    fn unwatch(&mut self, path: &Path) {
        use notify::Watcher;
        // The OS may already have dropped the watch of a removed or replaced path.
        let _ = self.watcher.unwatch(path);
    }
    fn take_moved(&mut self) -> Vec<PathBuf> {
        self.moved
            .lock()
            .map(|mut moved| moved.split_off(0))
            .unwrap_or_default()
    }
}

/// Watches with the platform's native API. If it cannot start, the returned
/// watch is never complete, so the UI falls back to polling.
pub fn native(on_change: impl Fn() + Send + 'static) -> DiskWatch {
    let (sender, events) = mpsc::channel();
    let moved = Arc::new(Mutex::new(Vec::new()));
    let record = moved.clone();
    // Errors, including event queue overflow, also request a full rescan.
    let backend = notify::recommended_watcher(move |event: notify::Result<notify::Event>| {
        if let Ok(event) = &event {
            if !relevant(&event.kind) {
                return;
            }
            if moves(&event.kind) {
                if let Ok(mut record) = record.lock() {
                    record.extend(event.paths.iter().cloned());
                }
            }
        }
        let _ = sender.send(());
    })
    .ok()
    .map(|watcher| Box::new(Native { watcher, moved }) as Box<dyn Backend>);
    let debounce = Debounce::new(Duration::from_millis(150), Duration::from_secs(1));
    thread::spawn(move || forward(events, debounce, on_change));
    DiskWatch::new(backend)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::DiskChange;

    #[derive(Default)]
    struct Log {
        watched: Vec<PathBuf>,
        calls: Vec<String>,
        limit: Option<usize>,
        moved: Vec<PathBuf>,
    }
    struct Fake(Arc<Mutex<Log>>);
    impl Backend for Fake {
        fn watch(&mut self, path: &Path) -> Result<(), WatchError> {
            let mut log = self.0.lock().unwrap();
            log.calls.push(format!("watch {}", path.display()));
            if log.limit == Some(log.watched.len()) {
                return Err(WatchError::Limit);
            }
            log.watched.push(path.to_path_buf());
            Ok(())
        }
        fn unwatch(&mut self, path: &Path) {
            let mut log = self.0.lock().unwrap();
            log.calls.push(format!("unwatch {}", path.display()));
            log.watched.retain(|p| p != path);
        }
        fn take_moved(&mut self) -> Vec<PathBuf> {
            self.0.lock().unwrap().moved.split_off(0)
        }
    }
    fn fake() -> (DiskWatch, Arc<Mutex<Log>>) {
        let log = Arc::new(Mutex::new(Log::default()));
        (DiskWatch::new(Some(Box::new(Fake(log.clone())))), log)
    }
    fn reconcile(watch: &mut DiskWatch, session: &Session) -> bool {
        let folders = session.vault().unwrap().list().unwrap().1;
        watch.reconcile(&targets(session, &folders))
    }
    fn vault() -> (tempfile::TempDir, PathBuf, Session) {
        let dir = tempfile::tempdir().unwrap();
        let root = fs::canonicalize(dir.path()).unwrap();
        fs::create_dir_all(root.join("A/B")).unwrap();
        fs::write(root.join("A/B/Note.md"), "one\n").unwrap();
        fs::write(root.join("Top.md"), "top\n").unwrap();
        let mut session = Session::new();
        session.open_vault(&root).unwrap();
        session.select("A/B/Note.md").unwrap();
        (dir, root, session)
    }
    fn sorted(log: &Arc<Mutex<Log>>) -> Vec<PathBuf> {
        let mut watched = log.lock().unwrap().watched.clone();
        watched.sort();
        watched
    }

    #[test]
    fn watches_note_and_folders_without_duplicates() {
        let (_dir, root, session) = vault();
        let (mut watch, log) = fake();
        assert!(reconcile(&mut watch, &session));
        let note = root.join("A/B/Note.md");
        assert_eq!(
            sorted(&log),
            vec![root.clone(), root.join("A"), root.join("A/B"), note]
        );
        let calls = log.lock().unwrap().calls.len();
        assert!(reconcile(&mut watch, &session));
        assert_eq!(log.lock().unwrap().calls.len(), calls, "no churn");
    }

    #[test]
    fn changed_note_keeps_watch_and_conflicts_only_when_dirty() {
        let (_dir, root, mut session) = vault();
        let (mut watch, log) = fake();
        reconcile(&mut watch, &session);
        let calls = log.lock().unwrap().calls.len();
        fs::write(root.join("A/B/Note.md"), "two\n").unwrap();
        assert_eq!(session.sync_with_disk(), DiskChange::Reloaded);
        assert!(reconcile(&mut watch, &session));
        assert_eq!(log.lock().unwrap().calls.len(), calls);
        session.set_content("mine\n".into());
        fs::write(root.join("A/B/Note.md"), "three\n").unwrap();
        assert_eq!(session.sync_with_disk(), DiskChange::Conflict);
        assert_eq!(session.content(), "mine\n");
        session.merge_outside_changes().unwrap();
        assert!(session.dirty());
    }

    #[test]
    fn atomic_replacement_rewatches_the_note() {
        let (_dir, root, mut session) = vault();
        let (mut watch, log) = fake();
        reconcile(&mut watch, &session);
        let note = root.join("A/B/Note.md");
        let staged = root.join("A/B/.staged");
        fs::write(&staged, "replaced\n").unwrap();
        fs::rename(&staged, &note).unwrap();
        log.lock().unwrap().calls.clear();
        assert_eq!(session.sync_with_disk(), DiskChange::Reloaded);
        assert!(reconcile(&mut watch, &session));
        let note = note.display();
        assert_eq!(
            log.lock().unwrap().calls,
            vec![format!("unwatch {note}"), format!("watch {note}")]
        );
    }

    #[test]
    fn deleted_folder_falls_back_to_ancestor_until_recreated() {
        let (_dir, root, mut session) = vault();
        let (mut watch, log) = fake();
        reconcile(&mut watch, &session);
        session.set_content("unsaved\n".into());
        fs::remove_dir_all(root.join("A")).unwrap();
        assert_eq!(session.sync_with_disk(), DiskChange::Missing);
        assert!(reconcile(&mut watch, &session));
        assert_eq!(sorted(&log), vec![root.clone()]);
        assert_eq!(session.content(), "unsaved\n");

        fs::create_dir(root.join("A")).unwrap();
        assert_eq!(session.sync_with_disk(), DiskChange::Missing);
        reconcile(&mut watch, &session);
        assert_eq!(sorted(&log), vec![root.clone(), root.join("A")]);

        fs::create_dir(root.join("A/B")).unwrap();
        fs::write(root.join("A/B/Note.md"), "back\n").unwrap();
        assert_eq!(session.sync_with_disk(), DiskChange::Conflict);
        reconcile(&mut watch, &session);
        assert_eq!(sorted(&log).len(), 4);
        assert_eq!(session.content(), "unsaved\n");
    }

    #[test]
    fn recreated_folder_with_same_name_is_rewatched() {
        let (_dir, root, mut session) = vault();
        let (mut watch, log) = fake();
        reconcile(&mut watch, &session);
        let backup = root.join("A/old");
        fs::rename(root.join("A/B"), &backup).unwrap();
        fs::create_dir(root.join("A/B")).unwrap();
        fs::rename(backup.join("Note.md"), root.join("A/B/Note.md")).unwrap();
        fs::remove_dir(&backup).unwrap();
        log.lock().unwrap().calls.clear();
        assert_eq!(session.sync_with_disk(), DiskChange::None);
        reconcile(&mut watch, &session);
        let folder = root.join("A/B").display().to_string();
        let calls = log.lock().unwrap().calls.clone();
        assert!(calls.contains(&format!("unwatch {folder}")));
        assert!(calls.contains(&format!("watch {folder}")));
        assert_eq!(sorted(&log).len(), 4);
    }

    #[test]
    fn vault_switch_and_deselection_drop_old_watches() {
        let (_dir, root, mut session) = vault();
        let (mut watch, log) = fake();
        reconcile(&mut watch, &session);
        let other = tempfile::tempdir().unwrap();
        let other_root = fs::canonicalize(other.path()).unwrap();
        session.open_vault(&other_root).unwrap();
        reconcile(&mut watch, &session);
        assert_eq!(sorted(&log), vec![other_root]);
        assert!(!sorted(&log).contains(&root));
    }

    #[test]
    fn watch_limit_keeps_note_watched_and_requests_polling() {
        let (_dir, root, session) = vault();
        let (mut watch, log) = fake();
        log.lock().unwrap().limit = Some(2);
        assert!(!reconcile(&mut watch, &session));
        assert!(!watch.complete());
        assert_eq!(
            log.lock().unwrap().watched,
            vec![root.join("A/B/Note.md"), root.join("A/B")]
        );
        // Stops at the first limit error instead of retrying every folder.
        assert_eq!(log.lock().unwrap().calls.len(), 3);
        log.lock().unwrap().limit = None;
        assert!(reconcile(&mut watch, &session));
    }

    #[test]
    fn missing_backend_requests_polling() {
        let (_dir, _root, session) = vault();
        let mut watch = DiskWatch::new(None);
        assert!(!reconcile(&mut watch, &session));
    }

    #[test]
    fn debounce_coalesces_bursts_but_bounds_delay() {
        let start = Instant::now();
        let ms = |n| start + Duration::from_millis(n);
        let mut debounce = Debounce::new(Duration::from_millis(150), Duration::from_secs(1));
        assert_eq!(debounce.deadline(), None);
        debounce.event(ms(0));
        debounce.event(ms(100));
        assert!(!debounce.fire(ms(200)));
        assert!(debounce.fire(ms(250)));
        assert!(!debounce.fire(ms(260)), "fires once per burst");
        for n in (300..1400).step_by(100) {
            debounce.event(ms(n));
        }
        assert_eq!(debounce.deadline(), Some(ms(1300)));
        assert!(debounce.fire(ms(1300)));
    }

    #[test]
    fn forward_calls_back_once_per_burst_in_order() {
        let (sender, events) = mpsc::channel();
        let (fired, calls) = mpsc::channel();
        let debounce = Debounce::new(Duration::from_millis(200), Duration::from_secs(1));
        let thread =
            thread::spawn(move || forward(events, debounce, move || fired.send(()).unwrap()));
        for _ in 0..5 {
            sender.send(()).unwrap();
        }
        calls.recv_timeout(Duration::from_secs(5)).unwrap();
        drop(sender);
        thread.join().unwrap();
        assert!(calls.try_recv().is_err());
    }

    #[test]
    fn reported_moves_force_rewatch_despite_reused_inode() {
        let (_dir, root, session) = vault();
        let (mut watch, log) = fake();
        reconcile(&mut watch, &session);
        let folder = root.join("A/B");
        log.lock().unwrap().moved.push(folder.clone());
        log.lock().unwrap().calls.clear();
        assert!(reconcile(&mut watch, &session));
        let folder = folder.display();
        assert_eq!(
            log.lock().unwrap().calls,
            vec![format!("unwatch {folder}"), format!("watch {folder}")]
        );
    }

    #[test]
    fn missing_vault_folder_watches_its_ancestor() {
        let (_dir, root, mut session) = vault();
        let (mut watch, log) = fake();
        reconcile(&mut watch, &session);
        session.discard_edits();
        fs::remove_dir_all(&root).unwrap();
        assert!(watch.reconcile(&targets(&session, &[])));
        assert_eq!(sorted(&log), vec![root.parent().unwrap().to_path_buf()]);
    }

    #[test]
    fn read_only_access_does_not_request_a_rescan() {
        use notify::event::{CreateKind, ModifyKind, RemoveKind};
        assert!(!relevant(&EventKind::Access(AccessKind::Open(
            AccessMode::Any
        ))));
        assert!(!relevant(&EventKind::Access(AccessKind::Close(
            AccessMode::Read
        ))));
        assert!(relevant(&EventKind::Access(AccessKind::Close(
            AccessMode::Write
        ))));
        assert!(relevant(&EventKind::Modify(ModifyKind::Any)));
        assert!(relevant(&EventKind::Other));
        assert!(moves(&EventKind::Remove(RemoveKind::Folder)));
        assert!(moves(&EventKind::Create(CreateKind::File)));
        assert!(!moves(&EventKind::Modify(ModifyKind::Any)));
    }

    #[test]
    fn native_backend_reports_changes_without_feedback() {
        let (_dir, root, mut session) = vault();
        let (fired, calls) = mpsc::channel();
        let fired = Mutex::new(fired);
        let mut watch = native(move || {
            let _ = fired.lock().unwrap().send(());
        });
        assert!(reconcile(&mut watch, &session));
        let wait = || calls.recv_timeout(Duration::from_secs(10)).unwrap();
        let drain = || while calls.recv_timeout(Duration::from_millis(300)).is_ok() {};
        // Like the app: each callback syncs, rescans and reconciles, which must
        // not itself produce another callback.
        let mut settle = |watch: &mut DiskWatch| {
            wait();
            drain();
            session.sync_with_disk();
            assert!(reconcile(watch, &session));
            assert!(
                calls.recv_timeout(Duration::from_millis(500)).is_err(),
                "a rescan triggered another"
            );
        };
        let note = root.join("A/B/Note.md");
        fs::write(&note, "changed\n").unwrap();
        settle(&mut watch);
        fs::write(root.join("A/B/.staged"), "replaced\n").unwrap();
        fs::rename(root.join("A/B/.staged"), &note).unwrap();
        settle(&mut watch);
        // Fast deletion and recreation often reuses the folder's inode number.
        fs::remove_dir_all(root.join("A/B")).unwrap();
        fs::create_dir(root.join("A/B")).unwrap();
        fs::write(&note, "recreated\n").unwrap();
        settle(&mut watch);
        fs::write(root.join("A/B/New.md"), "").unwrap();
        settle(&mut watch);
        fs::remove_dir_all(root.join("A")).unwrap();
        settle(&mut watch);
        fs::create_dir(root.join("A")).unwrap();
        wait();
    }
}
