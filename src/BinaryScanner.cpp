#include "BinaryScanner.h"

#include <cstdio>
#include <cstring>

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace BinaryScanner {

namespace {

// GUI-toolkit library substrings (RESEARCH.md Code Examples / A3). A binary
// is classified GUI if any DT_NEEDED name *contains* one of these.
const char* const kGuiToolkitSubstrings[] = {
    "libX11", "libgtk-3", "libgtk-4", "libQt5",
    "libQt6", "libwayland-client", "libSDL2",
};

// Tiny RAII guard for an mmap()'d region local to this translation unit.
// Deliberately not Manager.h's FdGuard -- that would pull X11 headers into
// this X11-free module.
struct MmapGuard {
    void* base = nullptr;
    size_t len = 0;

    ~MmapGuard() {
        if (base != nullptr && base != MAP_FAILED) {
            munmap(base, len);
        }
    }
};

}  // namespace

std::optional<std::vector<std::string>> readNeededLibraries(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return std::nullopt;
    }

    // T-7-04: derive size/mode from the already-open fd via fstat(), never a
    // separate stat() on the path -- closes the TOCTOU window between the
    // permission check and the actual read.
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return std::nullopt;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < static_cast<off_t>(sizeof(Elf64_Ehdr))) {
        close(fd);
        return std::nullopt;
    }

    MmapGuard mapping;
    mapping.len = static_cast<size_t>(st.st_size);
    mapping.base = mmap(nullptr, mapping.len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);  // mapping remains valid after close() per mmap() semantics

    if (mapping.base == MAP_FAILED) {
        return std::nullopt;
    }

    const unsigned char* base = static_cast<const unsigned char*>(mapping.base);
    const size_t fileSize = mapping.len;

    if (base[0] != ELFMAG0 || base[1] != ELFMAG1 || base[2] != ELFMAG2 || base[3] != ELFMAG3) {
        return std::nullopt;
    }

    const Elf64_Ehdr* ehdr = reinterpret_cast<const Elf64_Ehdr*>(base);
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        std::fprintf(stderr, "wm2: warning: BinaryScanner: skipping non-64-bit ELF %s\n", path.c_str());
        return std::nullopt;
    }

    // Bounds-check the program header table before touching it.
    const uint64_t phoff = ehdr->e_phoff;
    const uint64_t phnum = ehdr->e_phnum;
    const uint64_t phentsize = ehdr->e_phentsize;
    if (phentsize < sizeof(Elf64_Phdr)) {
        return std::nullopt;
    }
    if (phoff > fileSize || phnum * phentsize > fileSize - phoff) {
        return std::nullopt;
    }

    bool haveDynamic = false;
    uint64_t dynamicOffset = 0;
    uint64_t dynamicFilesz = 0;

    struct LoadSegment {
        uint64_t vaddr;
        uint64_t memsz;
        uint64_t offset;
    };
    std::vector<LoadSegment> loadSegments;

    for (uint64_t i = 0; i < phnum; ++i) {
        const Elf64_Phdr* phdr = reinterpret_cast<const Elf64_Phdr*>(base + phoff + i * phentsize);
        if (phdr->p_type == PT_DYNAMIC) {
            haveDynamic = true;
            dynamicOffset = phdr->p_offset;
            dynamicFilesz = phdr->p_filesz;
        } else if (phdr->p_type == PT_LOAD) {
            loadSegments.push_back(LoadSegment{phdr->p_vaddr, phdr->p_memsz, phdr->p_offset});
        }
    }

    if (!haveDynamic) {
        // Statically-linked binary -- nothing to classify, not an error.
        return std::nullopt;
    }
    if (dynamicOffset > fileSize || dynamicFilesz > fileSize - dynamicOffset) {
        return std::nullopt;
    }

    const size_t dynCount = dynamicFilesz / sizeof(Elf64_Dyn);
    const Elf64_Dyn* dynBase = reinterpret_cast<const Elf64_Dyn*>(base + dynamicOffset);

    // First pass: find DT_STRTAB's vaddr, translate to a file offset via the
    // PT_LOAD segments gathered above.
    bool haveStrtabVaddr = false;
    uint64_t strtabVaddr = 0;
    for (size_t i = 0; i < dynCount; ++i) {
        const Elf64_Dyn& dyn = dynBase[i];
        if (dyn.d_tag == DT_NULL) {
            break;
        }
        if (dyn.d_tag == DT_STRTAB) {
            haveStrtabVaddr = true;
            strtabVaddr = dyn.d_un.d_ptr;
            break;
        }
    }
    if (!haveStrtabVaddr) {
        return std::nullopt;
    }

    bool haveStrtabOffset = false;
    uint64_t strtabFileOffset = 0;
    for (const LoadSegment& seg : loadSegments) {
        if (strtabVaddr >= seg.vaddr && strtabVaddr < seg.vaddr + seg.memsz) {
            strtabFileOffset = seg.offset + (strtabVaddr - seg.vaddr);
            haveStrtabOffset = true;
            break;
        }
    }
    if (!haveStrtabOffset || strtabFileOffset > fileSize) {
        return std::nullopt;
    }

    // Second pass: collect DT_NEEDED entries, resolving each d_un.d_val as a
    // byte offset into the .dynstr region located above.
    std::vector<std::string> needed;
    for (size_t i = 0; i < dynCount; ++i) {
        const Elf64_Dyn& dyn = dynBase[i];
        if (dyn.d_tag == DT_NULL) {
            break;
        }
        if (dyn.d_tag != DT_NEEDED) {
            continue;
        }

        const uint64_t nameOffset = strtabFileOffset + dyn.d_un.d_val;
        if (nameOffset >= fileSize) {
            continue;  // out-of-range, skip this entry rather than crash
        }

        // Bounds-checked null-terminated string read.
        const char* strStart = reinterpret_cast<const char*>(base + nameOffset);
        size_t maxLen = fileSize - nameOffset;
        size_t len = 0;
        while (len < maxLen && strStart[len] != '\0') {
            ++len;
        }
        if (len == maxLen) {
            continue;  // not null-terminated within file bounds, skip
        }
        needed.emplace_back(strStart, len);
    }

    return needed;
}

bool isGuiBinary(const std::vector<std::string>& neededLibs) {
    for (const std::string& lib : neededLibs) {
        for (const char* toolkit : kGuiToolkitSubstrings) {
            if (lib.find(toolkit) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace BinaryScanner
