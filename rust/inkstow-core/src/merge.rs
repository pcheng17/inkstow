//! Line-based three-way merge with the Qt prototype's LCS tie-breaking and markers.
#[derive(Clone)]
struct Change {
    start: usize,
    end: usize,
    lines: Vec<String>,
}
fn lines(text: &str) -> Vec<String> {
    text.split('\n').map(str::to_owned).collect()
}
fn changes(base: &[String], other: &[String]) -> Vec<Change> {
    let (n, m) = (base.len(), other.len());
    let mut lcs = vec![vec![0usize; m + 1]; n + 1];
    for i in (0..n).rev() {
        for j in (0..m).rev() {
            lcs[i][j] = if base[i] == other[j] {
                1 + lcs[i + 1][j + 1]
            } else {
                lcs[i + 1][j].max(lcs[i][j + 1])
            };
        }
    }
    let (mut i, mut j) = (0, 0);
    let mut result = Vec::new();
    while i < n || j < m {
        if i < n && j < m && base[i] == other[j] {
            i += 1;
            j += 1;
            continue;
        }
        let start = i;
        let mut replacement = Vec::new();
        while i < n || j < m {
            if i < n && j < m && base[i] == other[j] {
                break;
            }
            if j < m && (i == n || lcs[i][j + 1] >= lcs[i + 1][j]) {
                replacement.push(other[j].clone());
                j += 1;
            } else {
                i += 1;
            }
        }
        result.push(Change {
            start,
            end: i,
            lines: replacement,
        });
    }
    result
}
fn overlaps(a: &Change, b: &Change) -> bool {
    if a.start == a.end && b.start == b.end {
        return a.start == b.start;
    }
    if a.start == a.end {
        return b.start <= a.start && a.start < b.end;
    }
    if b.start == b.end {
        return a.start <= b.start && b.start < a.end;
    }
    a.start < b.end && b.start < a.end
}
fn apply(base: &[String], start: usize, end: usize, hunks: &[Change]) -> Vec<String> {
    let mut result = Vec::new();
    let mut pos = start;
    for hunk in hunks {
        result.extend_from_slice(&base[pos..hunk.start]);
        result.extend(hunk.lines.iter().cloned());
        pos = hunk.end;
    }
    result.extend_from_slice(&base[pos..end]);
    result
}
pub(crate) fn three_way(base_text: &str, mine_text: &str, outside_text: &str) -> Option<String> {
    let base = lines(base_text);
    let mine_lines = lines(mine_text);
    let outside_lines = lines(outside_text);
    const MAX_CELLS: usize = 2_000_000;
    if base.len() > MAX_CELLS / mine_lines.len() || base.len() > MAX_CELLS / outside_lines.len() {
        return None;
    }
    let mine = changes(&base, &mine_lines);
    let outside = changes(&base, &outside_lines);
    let (mut a, mut b, mut pos) = (0, 0, 0);
    let mut result = Vec::new();
    while a < mine.len() || b < outside.len() {
        let start = mine
            .get(a)
            .map_or(base.len(), |c| c.start)
            .min(outside.get(b).map_or(base.len(), |c| c.start));
        result.extend_from_slice(&base[pos..start]);
        let (mut left, mut right) = (Vec::new(), Vec::new());
        if a < mine.len() && (b == outside.len() || mine[a].start <= outside[b].start) {
            left.push(mine[a].clone());
            a += 1;
        } else {
            right.push(outside[b].clone());
            b += 1;
        }
        loop {
            let mut expanded = false;
            if a < mine.len() && right.iter().any(|c| overlaps(&mine[a], c)) {
                left.push(mine[a].clone());
                a += 1;
                expanded = true;
            }
            if b < outside.len() && left.iter().any(|c| overlaps(&outside[b], c)) {
                right.push(outside[b].clone());
                b += 1;
                expanded = true;
            }
            if !expanded {
                break;
            }
        }
        let end = left
            .iter()
            .chain(&right)
            .fold(start, |last, c| last.max(c.end));
        let l = apply(&base, start, end, &left);
        let r = apply(&base, start, end, &right);
        if left.is_empty() {
            result.extend(r);
        } else if right.is_empty() || l == r {
            result.extend(l);
        } else {
            result.push("<<<<<<< mine".to_owned());
            result.extend(l);
            result.push("=======".to_owned());
            result.extend(r);
            result.push(">>>>>>> outside".to_owned());
        }
        pos = end;
    }
    result.extend_from_slice(&base[pos..]);
    Some(result.join("\n"))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn separate_edits_combine_and_overlap_marks_conflict() {
        assert_eq!(
            three_way("a\nb\nc", "A\nb\nc", "a\nb\nC").unwrap(),
            "A\nb\nC"
        );
        assert_eq!(
            three_way("a\nb", "A\nb", "B\nb").unwrap(),
            "<<<<<<< mine\nA\n=======\nB\n>>>>>>> outside\nb"
        );
    }
    #[test]
    fn refuses_quadratic_merges() {
        let huge = "x\n".repeat(1500);
        assert!(three_way(&huge, &huge, &huge).is_none());
    }
}
