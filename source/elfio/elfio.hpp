/*
Copyright (C) 2001-present by Serge Lamikhov-Center

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef ELFIO_HPP
#define ELFIO_HPP

#include <string>
#include <functional>
#include <algorithm>
#include <array>
#include <vector>
#include <deque>
#include <memory>

#include <elfio/elf_types.hpp>
#include <elfio/elfio_version.hpp>
#include <elfio/elfio_utils.hpp>
#include <elfio/elfio_header.hpp>
#include <elfio/elfio_section.hpp>
#include <elfio/elfio_segment.hpp>
#include <elfio/elfio_strings.hpp>

#define ELFIO_HEADER_ACCESS_GET( TYPE, FNAME )         \
    TYPE get_##FNAME() const                           \
    {                                                  \
        return header ? ( header->get_##FNAME() ) : 0; \
    }

#define ELFIO_HEADER_ACCESS_GET_SET( TYPE, FNAME )     \
    TYPE get_##FNAME() const                           \
    {                                                  \
        return header ? ( header->get_##FNAME() ) : 0; \
    }                                                  \
    void set_##FNAME( TYPE val )                       \
    {                                                  \
        if ( header ) {                                \
            header->set_##FNAME( val );                \
        }                                              \
    }

namespace ELFIO {

//------------------------------------------------------------------------------
class elfio
{
  public:
    //------------------------------------------------------------------------------
    elfio() noexcept : sections( this ), segments( this )
    {
        create( ELFCLASS32, ELFDATA2LSB );
    }

    explicit elfio( compression_interface* compression ) noexcept
        : sections( this ), segments( this ),
          compression( std::shared_ptr<compression_interface>( compression ) )
    {
        elfio();
    }

    elfio( elfio&& other ) noexcept
        : sections( this ), segments( this ),
          current_file_pos( other.current_file_pos )
    {
        header          = std::move( other.header );
        sections_       = std::move( other.sections_ );
        segments_       = std::move( other.segments_ );
        convertor       = std::move( other.convertor );
        compression     = std::move( other.compression );

        other.header = nullptr;
        other.sections_.clear();
        other.segments_.clear();
        other.compression = nullptr;
    }

    elfio& operator=( elfio&& other ) noexcept
    {
        if ( this != &other ) {
            header           = std::move( other.header );
            sections_        = std::move( other.sections_ );
            segments_        = std::move( other.segments_ );
            convertor        = std::move( other.convertor );
            current_file_pos = other.current_file_pos;
            compression      = std::move( other.compression );

            other.current_file_pos = 0;
            other.header           = nullptr;
            other.compression      = nullptr;
            other.sections_.clear();
            other.segments_.clear();
        }
        return *this;
    }

    //------------------------------------------------------------------------------
    // clang-format off
    elfio( const elfio& )            = delete;
    elfio& operator=( const elfio& ) = delete;
    ~elfio()                         = default;
    // clang-format on

    //------------------------------------------------------------------------------
    void create( unsigned char file_class, unsigned char encoding )
    {
        sections_.clear();
        segments_.clear();
        convertor.setup( encoding );
        header = create_header( file_class, encoding );
        create_mandatory_sections();
    }

    //------------------------------------------------------------------------------
    bool load(const char * pBuffer, size_t pBufferSize)
    {
        sections_.clear();
        segments_.clear();

        std::array<char, EI_NIDENT> e_ident = { };
        // Read ELF file signature
        if(sizeof( e_ident ) > pBufferSize) {
            return false;
        }
        memcpy( e_ident.data(), pBuffer, sizeof( e_ident ) );

        // Is it ELF file?
        if (e_ident[EI_MAG0] != ELFMAG0 || e_ident[EI_MAG1] != ELFMAG1 ||
            e_ident[EI_MAG2] != ELFMAG2 || e_ident[EI_MAG3] != ELFMAG3 ) {
            return false;
        }

        if ( ( e_ident[EI_CLASS] != ELFCLASS64 ) &&
             ( e_ident[EI_CLASS] != ELFCLASS32 ) ) {
            return false;
        }

        if ( ( e_ident[EI_DATA] != ELFDATA2LSB ) &&
             ( e_ident[EI_DATA] != ELFDATA2MSB ) ) {
            return false;
        }

        convertor.setup( e_ident[EI_DATA] );
        header = create_header( e_ident[EI_CLASS], e_ident[EI_DATA] );
        if ( nullptr == header ) {
            return false;
        }
        if ( !header->load( pBuffer, pBufferSize ) ) {
            return false;
        }

        load_sections( pBuffer, pBufferSize );
        bool is_still_good = load_segments( pBuffer, pBufferSize );
        return is_still_good;
    }

    //------------------------------------------------------------------------------
    // ELF header access functions
    ELFIO_HEADER_ACCESS_GET( unsigned char, class );
    ELFIO_HEADER_ACCESS_GET( unsigned char, elf_version );
    ELFIO_HEADER_ACCESS_GET( unsigned char, encoding );
    ELFIO_HEADER_ACCESS_GET( Elf_Word, version );
    ELFIO_HEADER_ACCESS_GET( Elf_Half, header_size );
    ELFIO_HEADER_ACCESS_GET( Elf_Half, section_entry_size );
    ELFIO_HEADER_ACCESS_GET( Elf_Half, segment_entry_size );

    ELFIO_HEADER_ACCESS_GET_SET( unsigned char, os_abi );
    ELFIO_HEADER_ACCESS_GET_SET( unsigned char, abi_version );
    ELFIO_HEADER_ACCESS_GET_SET( Elf_Half, type );
    ELFIO_HEADER_ACCESS_GET_SET( Elf_Half, machine );
    ELFIO_HEADER_ACCESS_GET_SET( Elf_Word, flags );
    ELFIO_HEADER_ACCESS_GET_SET( Elf64_Addr, entry );
    ELFIO_HEADER_ACCESS_GET_SET( Elf64_Off, sections_offset );
    ELFIO_HEADER_ACCESS_GET_SET( Elf64_Off, segments_offset );
    ELFIO_HEADER_ACCESS_GET_SET( Elf_Half, section_name_str_index );

    //------------------------------------------------------------------------------
    const endianess_convertor& get_convertor() const { return convertor; }

  private:
    //------------------------------------------------------------------------------
    static bool is_offset_in_section( Elf64_Off offset, const section* sec )
    {
        return ( offset >= sec->get_offset() ) &&
               ( offset < ( sec->get_offset() + sec->get_size() ) );
    }

    //------------------------------------------------------------------------------
    std::unique_ptr<elf_header> create_header( unsigned char file_class,
                                               unsigned char encoding )
    {
        std::unique_ptr<elf_header> new_header;

        if ( file_class == ELFCLASS64 ) {
            new_header = std::unique_ptr<elf_header>(
                new ( std::nothrow ) elf_header_impl<Elf64_Ehdr>(
                    &convertor, encoding ) );
        }
        else if ( file_class == ELFCLASS32 ) {
            new_header = std::unique_ptr<elf_header>(
                new ( std::nothrow ) elf_header_impl<Elf32_Ehdr>(
                    &convertor, encoding ) );
        }
        else {
            return nullptr;
        }

        return new_header;
    }

    //------------------------------------------------------------------------------
    section* create_section()
    {
        unsigned char file_class = get_class();

        if ( file_class == ELFCLASS64 ) {
            sections_.emplace_back(
                new ( std::nothrow ) section_impl<Elf64_Shdr>(
                    &convertor, compression ) );
        }
        else if ( file_class == ELFCLASS32 ) {
            sections_.emplace_back(
                new ( std::nothrow ) section_impl<Elf32_Shdr>(
                    &convertor, compression ) );
        }
        else {
            sections_.pop_back();
            return nullptr;
        }

        section* new_section = sections_.back().get();
        new_section->set_index( static_cast<Elf_Half>( sections_.size() - 1 ) );

        return new_section;
    }

    //------------------------------------------------------------------------------
    segment* create_segment()
    {
        unsigned char file_class = header->get_class();

        if ( file_class == ELFCLASS64 ) {
            segments_.emplace_back(
                new ( std::nothrow )
                    segment_impl<Elf64_Phdr>( &convertor ) );
        }
        else if ( file_class == ELFCLASS32 ) {
            segments_.emplace_back(
                new ( std::nothrow )
                    segment_impl<Elf32_Phdr>( &convertor ) );
        }
        else {
            segments_.pop_back();
            return nullptr;
        }

        segment* new_segment = segments_.back().get();
        new_segment->set_index( static_cast<Elf_Half>( segments_.size() - 1 ) );

        return new_segment;
    }

    //------------------------------------------------------------------------------
    void create_mandatory_sections()
    {
        // Create null section without calling to 'add_section' as no string
        // section containing section names exists yet
        section* sec0 = create_section();
        sec0->set_index( 0 );
        sec0->set_name( "" );
        sec0->set_name_string_offset( 0 );

        set_section_name_str_index( 1 );
        section* shstrtab = sections.add( ".shstrtab" );
        shstrtab->set_type( SHT_STRTAB );
        shstrtab->set_addr_align( 1 );
    }

    //------------------------------------------------------------------------------
    bool load_sections( const char * pBuffer, size_t pBufferSize )
    {
        unsigned char file_class = header->get_class();
        Elf_Half      entry_size = header->get_section_entry_size();
        Elf_Half      num        = header->get_sections_num();
        Elf64_Off     offset     = header->get_sections_offset();

        if ( ( num != 0 && file_class == ELFCLASS64 &&
               entry_size < sizeof( Elf64_Shdr ) ) ||
             ( num != 0 && file_class == ELFCLASS32 &&
               entry_size < sizeof( Elf32_Shdr ) ) ) {
            return false;
        }

        for ( Elf_Half i = 0; i < num; ++i ) {
            section* sec = create_section();
            sec->load( pBuffer, pBufferSize,
                       static_cast<off_t>( offset ) +
                           static_cast<off_t>( i ) * entry_size);
            // To mark that the section is not permitted to reassign address
            // during layout calculation
            sec->set_address( sec->get_address() );
        }

        Elf_Half shstrndx = get_section_name_str_index();

        if ( SHN_UNDEF != shstrndx ) {
            string_section_accessor str_reader( sections[shstrndx] );
            for ( Elf_Half i = 0; i < num; ++i ) {
                Elf_Word section_offset = sections[i]->get_name_string_offset();
                const char* p = str_reader.get_string( section_offset );
                if ( p != nullptr ) {
                    sections[i]->set_name( p );
                }
            }
        }

        return true;
    }

    //------------------------------------------------------------------------------
    //! Checks whether the addresses of the section entirely fall within the given segment.
    //! It doesn't matter if the addresses are memory addresses, or file offsets,
    //!  they just need to be in the same address space
    static bool is_sect_in_seg( Elf64_Off sect_begin,
                                Elf_Xword sect_size,
                                Elf64_Off seg_begin,
                                Elf64_Off seg_end )
    {
        return ( seg_begin <= sect_begin ) &&
               ( sect_begin + sect_size <= seg_end ) &&
               ( sect_begin <
                 seg_end ); // this is important criteria when sect_size == 0
        // Example:  seg_begin=10, seg_end=12 (-> covering the bytes 10 and 11)
        //           sect_begin=12, sect_size=0  -> shall return false!
    }

    //------------------------------------------------------------------------------
    bool load_segments( const char * pBuffer, size_t pBufferSize )
    {
        unsigned char file_class = header->get_class();
        Elf_Half      entry_size = header->get_segment_entry_size();
        Elf_Half      num        = header->get_segments_num();
        Elf64_Off     offset     = header->get_segments_offset();

        if ( ( num != 0 && file_class == ELFCLASS64 &&
               entry_size < sizeof( Elf64_Phdr ) ) ||
             ( num != 0 && file_class == ELFCLASS32 &&
               entry_size < sizeof( Elf32_Phdr ) ) ) {
            return false;
        }

        for ( Elf_Half i = 0; i < num; ++i ) {
            if ( file_class == ELFCLASS64 ) {
                segments_.emplace_back(
                    new ( std::nothrow ) segment_impl<Elf64_Phdr>(
                        &convertor) );
            }
            else if ( file_class == ELFCLASS32 ) {
                segments_.emplace_back(
                    new ( std::nothrow ) segment_impl<Elf32_Phdr>(
                        &convertor ) );
            }
            else {
                segments_.pop_back();
                return false;
            }

            segment* seg = segments_.back().get();

            if ( !seg->load( pBuffer, pBufferSize,
                             static_cast<off_t>( offset ) +
                                 static_cast<off_t>( i ) * entry_size)) {
                segments_.pop_back();
                return false;
            }

            seg->set_index( i );

            // Add sections to the segments (similar to readelfs algorithm)
            Elf64_Off segBaseOffset = seg->get_offset();
            Elf64_Off segEndOffset  = segBaseOffset + seg->get_file_size();
            Elf64_Off segVBaseAddr  = seg->get_virtual_address();
            Elf64_Off segVEndAddr   = segVBaseAddr + seg->get_memory_size();
            for ( const auto& psec : sections ) {
                // SHF_ALLOC sections are matched based on the virtual address
                // otherwise the file offset is matched
                if ( ( ( psec->get_flags() & SHF_ALLOC ) == SHF_ALLOC )
                         ? is_sect_in_seg( psec->get_address(),
                                           psec->get_size(), segVBaseAddr,
                                           segVEndAddr )
                         : is_sect_in_seg( psec->get_offset(), psec->get_size(),
                                           segBaseOffset, segEndOffset ) ) {
                    // Alignment of segment shall not be updated, to preserve original value
                    // It will be re-calculated on saving.
                    seg->add_section_index( psec->get_index(), 0 );
                }
            }
        }

        return true;
    }

    //------------------------------------------------------------------------------
  public:
    class Sections
    {
      public:
        //------------------------------------------------------------------------------
        explicit Sections( elfio* parent ) : parent( parent ) {}

        //------------------------------------------------------------------------------
        Elf_Half size() const
        {
            return static_cast<Elf_Half>( parent->sections_.size() );
        }

        //------------------------------------------------------------------------------
        section* operator[]( unsigned int index ) const
        {
            section* sec = nullptr;

            if ( index < parent->sections_.size() ) {
                sec = parent->sections_[index].get();
            }

            return sec;
        }

        //------------------------------------------------------------------------------
        section* operator[]( const std::string& name ) const
        {
            section* sec = nullptr;

            for ( const auto& it : parent->sections_ ) {
                if ( it->get_name() == name ) {
                    sec = it.get();
                    break;
                }
            }

            return sec;
        }

        //------------------------------------------------------------------------------
        section* add( const std::string& name ) const
        {
            section* new_section = parent->create_section();
            new_section->set_name( name );

            Elf_Half str_index = parent->get_section_name_str_index();
            section* string_table( parent->sections_[str_index].get() );
            string_section_accessor str_writer( string_table );
            Elf_Word                pos = str_writer.add_string( name );
            new_section->set_name_string_offset( pos );

            return new_section;
        }

        //------------------------------------------------------------------------------
        std::vector<std::unique_ptr<section>>::iterator begin()
        {
            return parent->sections_.begin();
        }

        //------------------------------------------------------------------------------
        std::vector<std::unique_ptr<section>>::iterator end()
        {
            return parent->sections_.end();
        }

        //------------------------------------------------------------------------------
        std::vector<std::unique_ptr<section>>::const_iterator begin() const
        {
            return parent->sections_.cbegin();
        }

        //------------------------------------------------------------------------------
        std::vector<std::unique_ptr<section>>::const_iterator end() const
        {
            return parent->sections_.cend();
        }

        //------------------------------------------------------------------------------
      private:
        elfio* parent;
    };
    Sections sections;

    //------------------------------------------------------------------------------
    friend class Segments;
    class Segments
    {
      public:
        //------------------------------------------------------------------------------
        explicit Segments( elfio* parent ) : parent( parent ) {}

        //------------------------------------------------------------------------------
        Elf_Half size() const
        {
            return static_cast<Elf_Half>( parent->segments_.size() );
        }

        //------------------------------------------------------------------------------
        segment* operator[]( unsigned int index ) const
        {
            return parent->segments_[index].get();
        }

        //------------------------------------------------------------------------------
        segment* add() { return parent->create_segment(); }

        //------------------------------------------------------------------------------
        std::vector<std::unique_ptr<segment>>::iterator begin()
        {
            return parent->segments_.begin();
        }

        //------------------------------------------------------------------------------
        std::vector<std::unique_ptr<segment>>::iterator end()
        {
            return parent->segments_.end();
        }

        //------------------------------------------------------------------------------
        std::vector<std::unique_ptr<segment>>::const_iterator begin() const
        {
            return parent->segments_.cbegin();
        }

        //------------------------------------------------------------------------------
        std::vector<std::unique_ptr<segment>>::const_iterator end() const
        {
            return parent->segments_.cend();
        }

        //------------------------------------------------------------------------------
      private:
        elfio* parent;
    };
    Segments segments;

    //------------------------------------------------------------------------------
  private:
    std::unique_ptr<elf_header>            header  = nullptr;
    std::vector<std::unique_ptr<section>>  sections_;
    std::vector<std::unique_ptr<segment>>  segments_;
    endianess_convertor                    convertor;
    std::shared_ptr<compression_interface> compression = nullptr;

    Elf_Xword current_file_pos = 0;
};

} // namespace ELFIO

#include <elfio/elfio_symbols.hpp>
#include <elfio/elfio_note.hpp>
#include <elfio/elfio_relocation.hpp>
#include <elfio/elfio_dynamic.hpp>
#include <elfio/elfio_array.hpp>
#include <elfio/elfio_modinfo.hpp>
#include <elfio/elfio_versym.hpp>

#endif // ELFIO_HPP
