#import <Foundation/Foundation.h>

#include <filesystem>
#include <string>

namespace inkstow::portable
{
bool trashMac(const std::filesystem::path& path, std::string* error)
{
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        NSError* failure = nil;
        if ([[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:&failure])
            return true;
        *error = "Could not move the note to the system trash, so it was not deleted.";
        return false;
    }
}
} // namespace inkstow::portable
