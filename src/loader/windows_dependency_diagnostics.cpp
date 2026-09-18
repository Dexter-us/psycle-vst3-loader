#include "windows_dependency_diagnostics.h"

#if defined(_WIN32)

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace psycle::loader {

namespace {

constexpr std::uintmax_t kMaximumImageBytes = 256U * 1024U * 1024U;
constexpr size_t kMaximumReportedDependencies = 8;

class DllSearchDirectories final {
public:
    explicit DllSearchDirectories(const std::filesystem::path& machinePath) {
        add(machinePath.parent_path());
        add(machinePath.parent_path().parent_path());
    }

    ~DllSearchDirectories() {
        for (const auto cookie : cookies_) {
            RemoveDllDirectory(cookie);
        }
    }

private:
    std::vector<DLL_DIRECTORY_COOKIE> cookies_;

    void add(const std::filesystem::path& directory) {
        if (directory.empty()) {
            return;
        }
        if (const auto cookie = AddDllDirectory(directory.c_str())) {
            cookies_.push_back(cookie);
        }
    }
};

bool hasRange(size_t offset, size_t size, size_t total) {
    return offset <= total && size <= total - offset;
}

template <typename Type>
const Type* viewAt(const std::vector<uint8_t>& image, size_t offset) {
    return hasRange(offset, sizeof(Type), image.size())
        ? reinterpret_cast<const Type*>(image.data() + offset)
        : nullptr;
}

bool startsWithIgnoreCase(const std::string& value, const char* prefix) {
    size_t index = 0;
    for (; prefix[index] != '\0'; ++index) {
        if (index >= value.size() ||
            std::tolower(static_cast<unsigned char>(value[index])) !=
                std::tolower(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.c_str(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.c_str(),
        static_cast<int>(text.size()),
        result.data(),
        length
    );
    return result;
}

std::vector<std::string> readImportedDlls(
    const std::filesystem::path& path
) {
    std::error_code fileError;
    const auto fileSize = std::filesystem::file_size(path, fileError);
    if (fileError || fileSize == 0 || fileSize > kMaximumImageBytes ||
        fileSize > std::numeric_limits<size_t>::max()) {
        return {};
    }

    std::vector<uint8_t> image(static_cast<size_t>(fileSize));
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(
            reinterpret_cast<char*>(image.data()),
            static_cast<std::streamsize>(image.size())
        )) {
        return {};
    }

    const auto* dos = viewAt<IMAGE_DOS_HEADER>(image, 0);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew < 0) {
        return {};
    }

    const size_t ntOffset = static_cast<size_t>(dos->e_lfanew);
    const auto* signature = viewAt<DWORD>(image, ntOffset);
    const auto* fileHeader = viewAt<IMAGE_FILE_HEADER>(
        image,
        ntOffset + sizeof(DWORD)
    );
    if (!signature || *signature != IMAGE_NT_SIGNATURE || !fileHeader) {
        return {};
    }

    const size_t optionalOffset =
        ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    if (!hasRange(
            optionalOffset,
            fileHeader->SizeOfOptionalHeader,
            image.size()
        )) {
        return {};
    }

    DWORD importRva = 0;
    DWORD sizeOfHeaders = 0;
    const auto* magic = viewAt<WORD>(image, optionalOffset);
    if (!magic) {
        return {};
    }
    if (*magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        const auto* optional =
            viewAt<IMAGE_OPTIONAL_HEADER64>(image, optionalOffset);
        if (!optional ||
            optional->NumberOfRvaAndSizes <=
                IMAGE_DIRECTORY_ENTRY_IMPORT) {
            return {};
        }
        importRva =
            optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
                .VirtualAddress;
        sizeOfHeaders = optional->SizeOfHeaders;
    } else if (*magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        const auto* optional =
            viewAt<IMAGE_OPTIONAL_HEADER32>(image, optionalOffset);
        if (!optional ||
            optional->NumberOfRvaAndSizes <=
                IMAGE_DIRECTORY_ENTRY_IMPORT) {
            return {};
        }
        importRva =
            optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
                .VirtualAddress;
        sizeOfHeaders = optional->SizeOfHeaders;
    } else {
        return {};
    }
    if (importRva == 0) {
        return {};
    }

    const size_t sectionOffset =
        optionalOffset + fileHeader->SizeOfOptionalHeader;
    const auto rvaToOffset = [&](DWORD rva) -> size_t {
        if (rva < sizeOfHeaders && rva < image.size()) {
            return static_cast<size_t>(rva);
        }
        for (WORD index = 0;
             index < fileHeader->NumberOfSections;
             ++index) {
            const auto* section = viewAt<IMAGE_SECTION_HEADER>(
                image,
                sectionOffset +
                    static_cast<size_t>(index) *
                        sizeof(IMAGE_SECTION_HEADER)
            );
            if (!section) {
                break;
            }
            const DWORD span = std::max(
                section->Misc.VirtualSize,
                section->SizeOfRawData
            );
            if (rva >= section->VirtualAddress &&
                rva - section->VirtualAddress < span) {
                const size_t offset =
                    static_cast<size_t>(section->PointerToRawData) +
                    static_cast<size_t>(rva - section->VirtualAddress);
                return offset < image.size() ? offset : image.size();
            }
        }
        return image.size();
    };

    std::vector<std::string> imports;
    size_t descriptorOffset = rvaToOffset(importRva);
    for (size_t index = 0; index < 4096; ++index) {
        const auto* descriptor = viewAt<IMAGE_IMPORT_DESCRIPTOR>(
            image,
            descriptorOffset +
                index * sizeof(IMAGE_IMPORT_DESCRIPTOR)
        );
        if (!descriptor || descriptor->Name == 0) {
            break;
        }
        const size_t nameOffset = rvaToOffset(descriptor->Name);
        if (nameOffset >= image.size()) {
            continue;
        }
        const char* name =
            reinterpret_cast<const char*>(image.data() + nameOffset);
        const size_t maximumLength = image.size() - nameOffset;
        const auto* end = static_cast<const char*>(
            std::memchr(name, '\0', maximumLength)
        );
        if (end && end != name) {
            imports.emplace_back(name, end);
        }
    }
    return imports;
}

std::string diagnoseDependencies(const std::filesystem::path& machinePath) {
    const auto imports = readImportedDlls(machinePath);
    if (imports.empty()) {
        return " The machine import table could not identify the missing DLL.";
    }

    std::vector<std::string> failures;
    for (const auto& imported : imports) {
        if (startsWithIgnoreCase(imported, "api-ms-win-") ||
            startsWithIgnoreCase(imported, "ext-ms-")) {
            continue;
        }
        const auto wideName = utf8ToWide(imported);
        if (wideName.empty()) {
            continue;
        }
        HMODULE dependency = LoadLibraryExW(
            wideName.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                LOAD_LIBRARY_SEARCH_USER_DIRS
        );
        if (dependency) {
            FreeLibrary(dependency);
            continue;
        }
        failures.push_back(
            imported + " (error " + std::to_string(GetLastError()) + ")"
        );
        if (failures.size() >= kMaximumReportedDependencies) {
            break;
        }
    }

    if (failures.empty()) {
        return " All direct imports were found; a dependency of one of those "
               "DLLs is missing.";
    }

    std::ostringstream message;
    message << " Missing or unloadable: ";
    for (size_t index = 0; index < failures.size(); ++index) {
        if (index > 0) {
            message << ", ";
        }
        message << failures[index];
    }
    message << ".";
    return message.str();
}

} // namespace

HMODULE loadWindowsMachineModule(
    const std::wstring& path,
    DWORD& errorCode,
    std::string& dependencyDiagnostic
) {
    const std::filesystem::path machinePath(path);
    DllSearchDirectories searchDirectories(machinePath);
    HMODULE module = LoadLibraryExW(
        path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
            LOAD_LIBRARY_SEARCH_USER_DIRS
    );
    errorCode = module ? ERROR_SUCCESS : GetLastError();
    if (!module && errorCode == ERROR_MOD_NOT_FOUND) {
        dependencyDiagnostic = diagnoseDependencies(machinePath);
    }
    return module;
}

} // namespace psycle::loader

#endif