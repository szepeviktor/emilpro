#include "emilpro/database.hh"

#include "emilpro/i_binary_parser.hh"

using namespace emilpro;

bool
Database::ParseFile(std::string_view file_path)
{
    auto parser = IBinaryParser::FromFile(file_path);
    if (!parser)
    {
        return false;
    }
    m_disassembler = IDisassembler::CreateFromArchitecture(parser->GetMachine());
    if (!m_disassembler)
    {
        return false;
    }

    parser->ForAllSections([this](auto section) {
        m_sections.push_back(std::move(section));
        m_section_refs.push_back(*m_sections.back());
    });

    for (auto &section : m_sections)
    {
        section->Disassemble(*m_disassembler);
    }

    m_parsers.push_back(std::move(parser));

    return true;
}

std::span<const std::reference_wrapper<ISection>>
Database::GetSections() const
{
    return m_section_refs;
}

std::vector<Database::LookupResult>
Database::LookupByAddress(uint64_t address)
{
    return {};
}

std::vector<Database::LookupResult>
Database::LookupByName(std::string_view name)
{
    return {};
}
