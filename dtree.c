#define _XOPEN_SOURCE 500

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Global trackers
static int numFiles = 0;
static int numDirectories = 0;
static long long cumulativeSize = 0;
static char desiredSuffix[16] = "";
static char targetLocation[PATH_MAX] = "";
static int actionType = 0;

// Function prototypes declared
void displayProgramUsage();
int manageFileSystemNode(const char *nodePath, const struct stat *statusBuffer, int nodeType, struct FTW *fileTreeBuffer);
int cloneFile(const char *sourceFile, const char *destinationFile);
int buildIntermediaryDirectories(const char *fullPath);
char *createRelativePath(const char *absolutePath, const char *rootPath);

// Creates the specified directory and any necessary parent directories.
int buildIntermediaryDirectories(const char *fullPath)
{
    char modifiedPath[PATH_MAX];
    char *pathSegment;
    size_t pathLength;

    snprintf(modifiedPath, sizeof(modifiedPath), "%s", fullPath);
    pathLength = strlen(modifiedPath);

    if (modifiedPath[pathLength - 1] == '/')
        modifiedPath[pathLength - 1] = 0;

    for (pathSegment = modifiedPath + 1; *pathSegment; ++pathSegment)
    {
        if (*pathSegment == '/')
        {
            *pathSegment = 0; // Temporarily null-terminate to create a parent directory
            mkdir(modifiedPath, 0755);
            *pathSegment = '/'; // Restore the slash
        }
    }
    return mkdir(modifiedPath, 0755);
}

// Generates path relative to specified base
char *createRelativePath(const char *absolutePath, const char *rootPath)
{
    size_t rootLength = strlen(rootPath);

    // Input validation:  must be under the root
    if (strncmp(absolutePath, rootPath, rootLength) == 0)
    {
        // Handle cases where the absolute path is directly under root, or a deeper subpath.
        if (absolutePath[rootLength] == '/')
            return (char *)absolutePath + rootLength + 1;
        else
            return (char *)absolutePath + rootLength;
    }
    return NULL;
}

// Copies files from source to destination directory.
int cloneFile(const char *sourceFile, const char *destinationFile)
{
    int sourceDescriptor, destinationDescriptor;
    char buffer[8192]; // increased buffer size
    ssize_t bytesRead, bytesWritten;

    sourceDescriptor = open(sourceFile, O_RDONLY);
    if (sourceDescriptor < 0)
        return -1;

    destinationDescriptor =
        open(destinationFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destinationDescriptor < 0)
    {
        close(sourceDescriptor);
        return -1;
    }

    while ((bytesRead = read(sourceDescriptor, buffer, sizeof(buffer))) > 0)
    {
        char *outputPointer = buffer;
        ssize_t totalBytesWritten = 0;

        do
        {
            bytesWritten = write(destinationDescriptor, outputPointer, bytesRead);
            if (bytesWritten >= 0)
            {
                bytesRead -= bytesWritten;
                outputPointer += bytesWritten;
                totalBytesWritten += bytesWritten;
            }
            else if (errno != EINTR)
            {
                close(sourceDescriptor);
                close(destinationDescriptor);
                return -1;
            }
        } while (bytesRead > 0);
    }

    close(sourceDescriptor);
    close(destinationDescriptor);
    return 0;
}

// Callback for nftw, operates on files/directories
int manageFileSystemNode(const char *nodePath, const struct stat *statusBuffer,
                         int nodeType, struct FTW *fileTreeBuffer)
{
    char destinationPath[PATH_MAX];
    const char *suffix;
    char *relativePath;

    switch (actionType)
    {
    case 1: // -ls: List path
        printf("%s\n", nodePath);
        break;

    case 2: // -ext: List files w/ extension
        if (nodeType == FTW_F)
        {
            suffix = strrchr(nodePath, '.');
            if (suffix && strcmp(suffix, desiredSuffix) == 0)
            {
                char absolutePath[PATH_MAX];
                realpath(nodePath, absolutePath);
                printf("%s\n", absolutePath);
            }
        }
        break;

    case 3: // -fc: Count files
        if (nodeType == FTW_F)
        {
            ++numFiles;
        }
        break;

    case 4: // -dc: Count directories
        if (nodeType == FTW_D)
        {
            ++numDirectories;
        }
        break;

    case 5: // -fs: Calculate size
        if (nodeType == FTW_F)
        {
            cumulativeSize += statusBuffer->st_size;
        }
        break;

    case 6: // -cp: Copy files
        if (nodeType == FTW_F)
        {
            suffix = strrchr(nodePath, '.');

            // Skip copying files having specified suffix
            if (strlen(desiredSuffix) > 0 && suffix &&
                strcmp(suffix, desiredSuffix) == 0)
                return 0;

            relativePath = createRelativePath(nodePath, targetLocation);
            if (relativePath)
            {
                snprintf(destinationPath, PATH_MAX, "%s/%s", targetLocation,
                         relativePath);
                char directoryPath[PATH_MAX];
                strcpy(directoryPath, destinationPath);
                dirname(directoryPath); // POSIX function

                buildIntermediaryDirectories(directoryPath); // Ensure destination dir
                                                             // exists
                if (cloneFile(nodePath, destinationPath) != 0)
                    fprintf(stderr, "Unable to copy %s\n", nodePath);
            }
        }
        else if (nodeType == FTW_D)
        {
            relativePath = createRelativePath(nodePath, targetLocation);
            if (relativePath)
            {
                snprintf(destinationPath, PATH_MAX, "%s/%s", targetLocation,
                         relativePath);
                buildIntermediaryDirectories(destinationPath);
            }
        }
        break;

    case 7: // -mv: Move files
        if (nodeType == FTW_F)
        {
            relativePath = createRelativePath(nodePath, targetLocation);
            if (relativePath)
            {
                snprintf(destinationPath, PATH_MAX, "%s/%s", targetLocation,
                         relativePath);
                char directoryPath[PATH_MAX];
                strcpy(directoryPath, destinationPath);
                dirname(directoryPath);

                buildIntermediaryDirectories(directoryPath);
                if (cloneFile(nodePath, destinationPath) == 0)
                    unlink(nodePath);
                else
                    fprintf(stderr, "Error on move: %s\n", nodePath);
            }
        }
        else if (nodeType == FTW_D && strcmp(nodePath, targetLocation) != 0)
        {
            relativePath = createRelativePath(nodePath, targetLocation);
            if (relativePath)
            {
                snprintf(destinationPath, PATH_MAX, "%s/%s", targetLocation,
                         relativePath);
                buildIntermediaryDirectories(destinationPath);
            }
        }
        break;

    case 8: // -del: Delete specified files
        if (nodeType == FTW_F)
        {
            suffix = strrchr(nodePath, '.');
            if (suffix && strcmp(suffix, desiredSuffix) == 0)
            {
                if (unlink(nodePath) != 0)
                    fprintf(stderr, "Failed to delete: %s\n", nodePath);
            }
        }
        break;
    }
    return 0;
}

void displayProgramUsage()
{
    printf("Syntax:\n");
    printf("  file_manager -ls [directory_path]\n");
    printf("  file_manager -ext [directory_path] [file_extension]\n");
    printf("  file_manager -fc [directory_path]\n");
    printf("  file_manager -dc [directory_path]\n");
    printf("  file_manager -fs [directory_path]\n");
    printf("  file_manager -cp [source_directory] [destination_directory] "
           "[file_extension]\n");
    printf("  file_manager -mv [source_directory] [destination_directory]\n");
    printf("  file_manager -del [directory_path] [file_extension]\n");
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        displayProgramUsage();
        return 1;
    }

    // Initialize
    numFiles = 0;
    numDirectories = 0;
    cumulativeSize = 0;
    memset(desiredSuffix, 0, sizeof(desiredSuffix));
    memset(targetLocation, 0, sizeof(targetLocation));

    // Ensure root exists
    struct stat rootStatus;
    if (stat(argv[2], &rootStatus) != 0 || !S_ISDIR(rootStatus.st_mode))
    {
        fprintf(stderr, "Error: %s is not a directory.\n", argv[2]);
        return 1;
    }

    // Interpret command-line
    if (strcmp(argv[1], "-ls") == 0)
        actionType = 1;
    else if (strcmp(argv[1], "-ext") == 0)
    {
        if (argc < 4)
        {
            displayProgramUsage();
            return 1;
        }
        actionType = 2;
        strncpy(desiredSuffix, argv[3], sizeof(desiredSuffix) - 1);
    }
    else if (strcmp(argv[1], "-fc") == 0)
        actionType = 3;
    else if (strcmp(argv[1], "-dc") == 0)
        actionType = 4;
    else if (strcmp(argv[1], "-fs") == 0)
        actionType = 5;
    else if (strcmp(argv[1], "-cp") == 0)
    {
        if (argc < 4)
        {
            displayProgramUsage();
            return 1;
        }
        actionType = 6;
        strncpy(targetLocation, argv[3], sizeof(targetLocation) - 1);
        if (argc > 4)
        {
            strncpy(desiredSuffix, argv[4], sizeof(desiredSuffix) - 1);
        }
        buildIntermediaryDirectories(targetLocation); // Ensure destination directory
                                                      // exists
    }
    else if (strcmp(argv[1], "-mv") == 0)
    {
        if (argc < 4)
        {
            displayProgramUsage();
            return 1;
        }
        actionType = 7;
        strncpy(targetLocation, argv[3], sizeof(targetLocation) - 1);
        buildIntermediaryDirectories(targetLocation);
    }
    else if (strcmp(argv[1], "-del") == 0)
    {
        if (argc < 4)
        {
            displayProgramUsage();
            return 1;
        }
        actionType = 8;
        strncpy(desiredSuffix, argv[3], sizeof(desiredSuffix) - 1);
    }

    else
    {
        displayProgramUsage();
        return 1;
    }

    // Initiate traversal
    if (nftw(argv[2], manageFileSystemNode, 20, FTW_PHYS) == -1)
    {
        perror("nftw");
        return 1;
    }

    // Produce results
    switch (actionType)
    {
    case 3:
        printf("Total files: %d\n", numFiles);
        break;
    case 4:
        printf("Total directories: %d\n", numDirectories);
        break;
    case 5:
        printf("Total size: %lld bytes\n", cumulativeSize);
        break;
    }

    // Cleaning step for moves.   Errors can occur, so report only
    if (actionType == 7)
    {
        if (rmdir(argv[2]) != 0)
        {
            fprintf(stderr,
                    "Warning: Unable to delete source, directory might not be empty. \n");
        }
    }

    return 0;
}