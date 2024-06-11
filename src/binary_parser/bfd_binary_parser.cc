#include "bfd_binary_parser.hh"

#include "section.hh"
#include "symbol.hh"

#include <array>
#include <bfd.h>
#include <cassert>
#include <fcntl.h>
#include <fmt/format.h>
#include <list>
#include <map>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

using namespace emilpro;

// ELF machine to the Machine enum mapping
constexpr auto kMachineMap = std::array {
    std::pair {bfd_arch_i386, Machine::kX86},
    std::pair {bfd_arch_mips, Machine::kMips},
    std::pair {bfd_arch_powerpc, Machine::kPpc},
    std::pair {bfd_arch_arm, Machine::kArm},
    std::pair {bfd_arch_aarch64, Machine::kArm64},
};


struct target_buffer
{
    uint8_t* base;
    size_t size;
};

// Helpers for libbfd
/* Opening the file is a no-op.  */
static void*
mem_bfd_iovec_open(struct bfd* abfd, void* open_closure)
{
    return open_closure;
}

/* Closing the file is just freeing the base/size pair on our side.  */

static int
mem_bfd_iovec_close(struct bfd* abfd, void* stream)
{
    free(stream);
    return 1;
}

/* For reading the file, we just need to pass through to target_read_memory and
   fix up the arguments and return values.  */

static file_ptr
mem_bfd_iovec_pread(struct bfd* abfd, void* stream, void* buf, file_ptr nbytes, file_ptr offset)
{
    struct target_buffer* buffer = (struct target_buffer*)stream;

    /* If this read will read all of the file, limit it to just the rest.  */
    if (offset + nbytes > (ssize_t)buffer->size)
        nbytes = buffer->size - offset;

    /* If there are no more bytes left, we've reached EOF.  */
    if (nbytes == 0)
        return 0;

    memcpy(buf, buffer->base + offset, nbytes);

    return nbytes;
}

/* For statting the file, we only support the st_size attribute.  */
static int
mem_bfd_iovec_stat(struct bfd* abfd, void* stream, struct stat* sb)
{
    struct target_buffer* buffer = (struct target_buffer*)stream;

    sb->st_size = buffer->size;
    return 0;
}


BfdBinaryParser::BfdBinaryParser(std::string_view path)
    : m_path(path)
    , m_rawData(NULL)
    , m_rawDataSize(0)
    , m_bfd(NULL)
    , m_bfd_syms(NULL)
    , m_dynamic_bfd_syms(NULL)
    , m_synthetic_bfd_syms(NULL)
{
}

BfdBinaryParser::~BfdBinaryParser()
{
    if (m_bfd)
    {
        free(m_bfd_syms);
        free(m_dynamic_bfd_syms);
        free(m_synthetic_bfd_syms);
        bfd_close(m_bfd);
        m_bfd = nullptr;
    }
}

Machine
BfdBinaryParser::GetMachine() const
{
    return m_machine;
}

void
BfdBinaryParser::ForAllSections(std::function<void(std::unique_ptr<ISection>)> on_section)
{
    for (auto& [bfd_section, sec] : m_pending_sections)
    {
        on_section(std::move(sec));
    }
    m_pending_sections.clear();
}


std::optional<Section::FileLine>
BfdBinaryParser::LookupLine(bfd_section* section, bfd_symbol** symTbl, uint64_t offset)
{
    const char* file_name;
    const char* function;
    unsigned int line_nr;

    // Use the section offset to lookup
    offset = offset - bfd_section_vma(section);

    if (bfd_find_nearest_line(m_bfd, section, symTbl, offset, &file_name, &function, &line_nr))
    {
        if (!file_name)
        {
            return std::nullopt;
        }

        return Section::FileLine {file_name, line_nr};
    }

    return std::nullopt;
}

// From ILineProvider
bool
BfdBinaryParser::getLineByAddress(uint64_t addr)
{
    //ILineProvider::FileLine out;
    bool out = false;

    BfdSectionByAddress_t::iterator it = m_sectionByAddress.lower_bound(addr);
    if (it == m_sectionByAddress.end())
    {
        return out;
    }

    bfd_section* section = it->second;

    if (!section)
        return out;

    //        if (lookupLine(&out, section, m_bfdSyms, addr))
    //            return out;
    //
    //        if (lookupLine(&out, section, m_dynamic_bfd_syms, addr))
    //            return out;

    return out;
}

bool
BfdBinaryParser::Parse()
{
    char** matching;
    unsigned int sz;
    struct target_buffer* buffer = (struct target_buffer*)malloc(sizeof(struct target_buffer));
    bool isElf;

    // Mmap file
    struct stat filestat;

    auto fd = open(m_path.data(), O_RDONLY);
    assert(fd != -1);
    if (fstat(fd, &filestat) != 0)
    {
        perror("stat failed");
        exit(1);
    }
    auto data = mmap(NULL, filestat.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        perror("mmap failed");
        exit(2);
    }

    buffer->base = (uint8_t*)data;
    buffer->size = filestat.st_size;
    m_rawData = (uint8_t*)data;
    m_rawDataSize = filestat.st_size;
    m_bfd = bfd_openr_iovec("",
                            NULL,
                            mem_bfd_iovec_open,
                            buffer,
                            mem_bfd_iovec_pread,
                            mem_bfd_iovec_close,
                            mem_bfd_iovec_stat);

    if (!m_bfd)
    {
        //            error("bfd_openr failed");
        return false;
    }
    if (!bfd_check_format_matches(m_bfd, bfd_object, &matching))
    {
        bfd_close(m_bfd);
        m_bfd = NULL;
        return false;
    }


    long symcount, dynsymcount, syntsymcount;
    bfd_symbol* syntheticSyms;

    symcount = bfd_read_minisymbols(m_bfd, FALSE, (void**)&m_bfd_syms, &sz);
    dynsymcount = bfd_read_minisymbols(m_bfd, TRUE /* dynamic */, (void**)&m_dynamic_bfd_syms, &sz);
    syntsymcount = bfd_get_synthetic_symtab(
        m_bfd, symcount, m_bfd_syms, dynsymcount, m_dynamic_bfd_syms, &syntheticSyms);


    // Create sections
    for (auto section = m_bfd->sections; section != NULL; section = section->next)
    {
        auto size = bfd_section_size(section);
        auto name = bfd_section_name(section);
        auto p = new bfd_byte[size];

        auto type = ISection::Type::kOther;

        std::string flags;

        if (section->flags & SEC_ALLOC)
        {
            flags += "A";
        }
        if (section->flags & SEC_LOAD)
        {
            flags += "L";
        }
        if (section->flags & SEC_RELOC)
        {
            flags += "r";
        }
        if (section->flags & SEC_READONLY)
        {
            flags += "RO";
        }
        if (section->flags & SEC_CODE)
        {
            flags += "C";
        }
        if (section->flags & SEC_DATA)
        {
            flags += "D";
        }
        if (section->flags & SEC_CONSTRUCTOR)
        {
            flags += "C";
        }
        if (section->flags & SEC_KEEP)
        {
            flags += "K";
        }


        std::unique_ptr<Section> sec;
        if (bfd_get_section_contents(m_bfd, section, p, 0, size))
        {
            if ((section->flags & SEC_CODE))
            {
                type = ISection::Type::kInstructions;
            }

            sec = std::make_unique<Section>(
                name,
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(p), size),
                bfd_section_vma(section),
                type,
                flags,
                [this, section](auto offset) { return LookupLine(section, m_bfd_syms, offset); });
        }
        else
        {
            sec = std::make_unique<Section>(name,
                                            std::span<const std::byte> {},
                                            bfd_section_vma(section),
                                            type,
                                            flags,
                                            [](auto offset) { return std::nullopt; });
        }

        m_pending_sections[section] = std::move(sec);
        delete[] p;
    }

    if (auto undef_section = bfd_und_section_ptr)
    {
        m_pending_sections[undef_section] =
            std::make_unique<Section>("*UNDEF*",
                                      std::span<const std::byte> {},
                                      0,
                                      ISection::Type::kOther,
                                      "U",
                                      [](auto offset) { return std::nullopt; });
    }


    handleSymbols(symcount, m_bfd_syms, false);
    handleSymbols(dynsymcount, m_dynamic_bfd_syms, true);
    if (syntheticSyms)
    {
        m_synthetic_bfd_syms = (bfd_symbol**)malloc(syntsymcount * sizeof(bfd_symbol*));
        for (long i = 0; i < syntsymcount; i++)
            m_synthetic_bfd_syms[i] = &syntheticSyms[i];
        handleSymbols(syntsymcount, m_synthetic_bfd_syms, false);
    }

    for (auto& [section, sec] : m_pending_sections)
    {
        sec->FixupSymbolSizes();
    }

    // The first pass has created symbols, now look for relocations
    for (auto section = m_bfd->sections; section != NULL; section = section->next)
    {
        HandleRelocations(section, m_bfd_syms);
        HandleRelocations(section, m_dynamic_bfd_syms);
        HandleRelocations(section, m_synthetic_bfd_syms);
    }

    auto arch = bfd_get_arch(m_bfd);

    auto it_machine = std::find_if(
        kMachineMap.begin(), kMachineMap.end(), [arch](auto& p) { return p.first == arch; });
    if (it_machine != kMachineMap.end())
    {
        m_machine = it_machine->second;
        if (m_machine == Machine::kArm && m_arm_in_thumb_mode)
        {
            m_machine = Machine::kArmThumb;
        }
    }

    // Add the file symbol
    //        ISymbol& sym = SymbolFactory::instance().createSymbol(ISymbol::LINK_NORMAL,
    //                                                              ISymbol::SYM_FILE,
    //                                                              "file",
    //                                                              data,
    //                                                              0,
    //                                                              dataSize,
    //                                                              0,
    //                                                              false,
    //                                                              false,
    //                                                              false,
    //                                                              0);
    //
    //        m_listener->onSymbol(sym);

    return true;
}

void
BfdBinaryParser::handleSymbols(long symcount, bfd_symbol** syms, bool dynamic)
{
    for (long i = 0; i < symcount; i++)
    {
        auto cur = syms[i];

        if (!cur)
        {
            continue;
        }

        // An interesting symbol?
        if (cur->flags & (BSF_DEBUGGING | BSF_FILE | BSF_WARNING))
        {
            continue;
        }

        auto sym_name = bfd_asymbol_name(cur);
        auto sym_addr = cur->value;
        auto section = bfd_asymbol_section(cur);

        if (bfd_get_arch(m_bfd) == bfd_arch_arm && sym_name)
        {
            // Skip ARM $a $d $t symbols
            if (strlen(sym_name) >= 2 && sym_name[0] == '$' && strchr("atd", sym_name[1]) &&
                (sym_name[2] == '\0' || sym_name[2] == '.'))
            {
                if (sym_name[1] == 't')
                {
                    m_arm_in_thumb_mode = true;
                }
                continue;
            }
        }

        auto sect_it = m_pending_sections.find(section);
        if (sect_it == m_pending_sections.end())
        {
            fmt::print("Dropping symbol {}, since it has no section\n", sym_name);
            continue;
        }

        std::string flags;

        if (dynamic)
        {
            flags += "D";
        }

        if (cur->flags & BSF_LOCAL)
        {
            flags += "L";
        }
        if (cur->flags & BSF_FUNCTION)
        {
            flags += "F";
        }
        if (cur->flags & BSF_WEAK)
        {
            flags += "W";
        }
        if (cur->flags & BSF_WEAK)
        {
            flags += "W";
        }
        if (bfd_is_und_section(cur->section))
        {
            flags += "U";
        }

        auto symbol = std::make_unique<Symbol>(*sect_it->second, sym_addr, flags, sym_name);
        m_symbol_map[cur] = symbol.get();
        sect_it->second->AddSymbol(std::move(symbol));
    }
}

void
BfdBinaryParser::HandleRelocations(asection* section, bfd_symbol** syms)
{

    if (bfd_is_abs_section(section) || bfd_is_und_section(section) || bfd_is_com_section(section) ||
        ((section->flags & SEC_RELOC) == 0))
    {
        return;
    }
    if ((section->flags & SEC_ALLOC) == 0)
    {
        return;
    }

    auto relsize = bfd_get_reloc_upper_bound(m_bfd, section);
    if (relsize == 0)
    {
        return;
    }

    auto sect_it = m_pending_sections.find(section);
    if (sect_it == m_pending_sections.end())
    {
        // A relocation for section we don't use
        return;
    }

    auto relpp = (arelent**)malloc(relsize);
    auto relcount = bfd_canonicalize_reloc(m_bfd, section, relpp, syms);

    if (relcount > 0)
    {
        auto sec = sect_it->second.get();
        for (auto p = relpp; relcount && *p != NULL; p++, relcount--)
        {
            arelent* q = *p;

            auto sym_it = m_symbol_map.find(*q->sym_ptr_ptr);
            if (sym_it == m_symbol_map.end())
            {
                continue;
            }
            auto sym = sym_it->second;
            sec->AddRelocation(q->address, *sym);
            sym->AddRelocation(*sec, q->address);
        }
    }
    free(relpp);
}
