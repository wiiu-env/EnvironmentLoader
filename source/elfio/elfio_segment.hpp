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

#ifndef ELFIO_SEGMENT_HPP
#define ELFIO_SEGMENT_HPP

#include <vector>
#include <new>
#include <limits>

namespace ELFIO {

class segment
{
    friend class elfio;

  public:
    virtual ~segment() = default;

    ELFIO_GET_ACCESS_DECL( Elf_Half, index );
    ELFIO_GET_SET_ACCESS_DECL( Elf_Word, type );
    ELFIO_GET_SET_ACCESS_DECL( Elf_Word, flags );
    ELFIO_GET_SET_ACCESS_DECL( Elf_Xword, align );
    ELFIO_GET_SET_ACCESS_DECL( Elf64_Addr, virtual_address );
    ELFIO_GET_SET_ACCESS_DECL( Elf64_Addr, physical_address );
    ELFIO_GET_SET_ACCESS_DECL( Elf_Xword, file_size );
    ELFIO_GET_SET_ACCESS_DECL( Elf_Xword, memory_size );
    ELFIO_GET_ACCESS_DECL( Elf64_Off, offset );

    virtual const char* get_data() const = 0;

    virtual Elf_Half add_section( section* psec, Elf_Xword addr_align ) = 0;
    virtual Elf_Half add_section_index( Elf_Half  index,
                                        Elf_Xword addr_align )          = 0;
    virtual Elf_Half get_sections_num() const                           = 0;
    virtual Elf_Half get_section_index_at( Elf_Half num ) const         = 0;
    virtual bool     is_offset_initialized() const                      = 0;

  protected:
    ELFIO_SET_ACCESS_DECL( Elf64_Off, offset );
    ELFIO_SET_ACCESS_DECL( Elf_Half, index );

    virtual const std::vector<Elf_Half>& get_sections() const = 0;

    virtual bool load( const char * pBuffer, size_t pBufferSize,
                       off_t header_offset )               = 0;
};

//------------------------------------------------------------------------------
template <class T> class segment_impl : public segment
{
  public:
    //------------------------------------------------------------------------------
    segment_impl( const endianess_convertor* convertor )
        : convertor( convertor )
    {
    }

    //------------------------------------------------------------------------------
    // Section info functions
    ELFIO_GET_SET_ACCESS( Elf_Word, type, ph.p_type );
    ELFIO_GET_SET_ACCESS( Elf_Word, flags, ph.p_flags );
    ELFIO_GET_SET_ACCESS( Elf_Xword, align, ph.p_align );
    ELFIO_GET_SET_ACCESS( Elf64_Addr, virtual_address, ph.p_vaddr );
    ELFIO_GET_SET_ACCESS( Elf64_Addr, physical_address, ph.p_paddr );
    ELFIO_GET_SET_ACCESS( Elf_Xword, file_size, ph.p_filesz );
    ELFIO_GET_SET_ACCESS( Elf_Xword, memory_size, ph.p_memsz );
    ELFIO_GET_ACCESS( Elf64_Off, offset, ph.p_offset );

    //------------------------------------------------------------------------------
    Elf_Half get_index() const override { return index; }

    //------------------------------------------------------------------------------
    const char* get_data() const override
    {
        return data.get();
    }

    //------------------------------------------------------------------------------
    Elf_Half add_section_index( Elf_Half  sec_index,
                                Elf_Xword addr_align ) override
    {
        sections.emplace_back( sec_index );
        if ( addr_align > get_align() ) {
            set_align( addr_align );
        }

        return (Elf_Half)sections.size();
    }

    //------------------------------------------------------------------------------
    Elf_Half add_section( section* psec, Elf_Xword addr_align ) override
    {
        return add_section_index( psec->get_index(), addr_align );
    }

    //------------------------------------------------------------------------------
    Elf_Half get_sections_num() const override
    {
        return (Elf_Half)sections.size();
    }

    //------------------------------------------------------------------------------
    Elf_Half get_section_index_at( Elf_Half num ) const override
    {
        if ( num < sections.size() ) {
            return sections[num];
        }

        return Elf_Half( -1 );
    }

    //------------------------------------------------------------------------------
  protected:
    //------------------------------------------------------------------------------

    //------------------------------------------------------------------------------
    void set_offset( const Elf64_Off& value ) override
    {
        ph.p_offset   = decltype( ph.p_offset )( value );
        ph.p_offset   = ( *convertor )( ph.p_offset );
        is_offset_set = true;
    }

    //------------------------------------------------------------------------------
    bool is_offset_initialized() const override { return is_offset_set; }

    //------------------------------------------------------------------------------
    const std::vector<Elf_Half>& get_sections() const override
    {
        return sections;
    }

    //------------------------------------------------------------------------------
    void set_index( const Elf_Half& value ) override { index = value; }

    //------------------------------------------------------------------------------
    bool load( const char * pBuffer, size_t pBufferSize,
               off_t header_offset) override
    {
        if( header_offset + sizeof( ph ) > pBufferSize ) {
            return false;
        }
        memcpy( reinterpret_cast<char*>( &ph ), pBuffer + header_offset, sizeof( ph ) );
        is_offset_set = true;

        return load_data(pBuffer, pBufferSize);
    }

    //------------------------------------------------------------------------------
    bool load_data(const char * pBuffer, size_t pBufferSize) const
    {
        if ( PT_NULL == get_type() || 0 == get_file_size() ) {
            return true;
        }
        auto offset = ( *convertor )( ph.p_offset );
        Elf_Xword size = get_file_size();

        if ( size > pBufferSize ) {
            data = nullptr;
        }
        else {
            data.reset( new ( std::nothrow ) char[(size_t)size + 1] );

            if ( nullptr != data.get()) {
                memcpy(data.get(), pBuffer + offset, size);
            }
            else {
                data = nullptr;
                return false;
            }
        }

        return true;
    }

    //------------------------------------------------------------------------------
  private:
    T                               ph      = {};
    Elf_Half                        index   = 0;
    mutable std::unique_ptr<char[]> data;
    std::vector<Elf_Half>           sections;
    const endianess_convertor*      convertor     = nullptr;
    bool                            is_offset_set = false;
};

} // namespace ELFIO

#endif // ELFIO_SEGMENT_HPP
