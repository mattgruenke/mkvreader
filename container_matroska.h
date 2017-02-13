#ifndef _CONTAINER_MATROSKA_H_
#define _CONTAINER_MATROSKA_H_

#include <list>

namespace foobar2000_io {

        // TO_DO:
    class service_base {};

    namespace matroska {
        class attachment;

        typedef std::list<attachment> attachment_list;
    }
    
    class container_matroska : public service_base {
    public:
        virtual void open(const char * p_path, bool p_info_only, abort_callback & p_abort)=0;
        virtual void open_file(service_ptr_t<file> & p_out, const filesystem::t_open_mode p_mode = filesystem::open_mode_read) const =0;
        virtual void get_display_path(pfc::string_base & p_out) const =0;
        virtual bool is_our_path(const char * p_path) const =0;
        virtual const matroska::attachment_list * get_attachment_list() const =0;

    public:
        static void g_open(service_ptr_t<container_matroska> & p_out, const char * p_path, bool p_info_only, abort_callback & p_abort) {
            service_enum_t<container_matroska> e;
            service_ptr_t<container_matroska> ptr;
            while(e.next(ptr)) {
                if (ptr->is_our_path(p_path)) {
		            ptr->open(p_path, p_info_only, p_abort);
                    p_out = ptr;
		            return;
                }
            }
	        throw exception_io_unsupported_format();
        }
        static bool g_is_our_path(const char * p_path) {
            service_enum_t<container_matroska> e;
            service_ptr_t<container_matroska> ptr;
            while(e.next(ptr)) {
                if (ptr->is_our_path(p_path)) {
		            return true;
                }
            }
            return false;
        }

        FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(container_matroska);
    };

    typedef service_ptr_t<container_matroska> container_matroska_ptr;

    namespace matroska {
        class attachment {
        private:
            container_matroska * m_owner;
            abort_callback * m_abort;
            pfc::string8 m_name, m_mime_type, m_description;
            t_size m_size;
            t_sfilesize m_position;

        public:
            attachment() : m_owner(0), m_abort(0) {};
            attachment(const container_matroska_ptr & p_owner, abort_callback & p_abort, const char * p_name, const char * p_mime_type,
                        const char * p_description, const t_size p_size, const t_sfilesize p_position)
                : m_name(p_name), m_mime_type(p_mime_type), m_description(p_description), m_size(p_size), m_position(p_position)
            {
                m_owner = p_owner.get_ptr();
                m_abort = &p_abort;
            };
            ~attachment() {};
            void get_name(pfc::string_base & p_out) const { p_out = m_name; };
            void get_mime_type(pfc::string_base & p_out) const { p_out = m_mime_type; };
            void get_description(pfc::string_base & p_out) const { p_out = m_description; };
            t_size get_size() const { return m_size; };
            t_sfilesize get_position() const { return m_position; };
            void get(void * p_buffer, t_sfilesize p_position, t_size p_size) const {
                service_ptr_t<file> file_ptr;
                m_owner->open_file(file_ptr);
                file_ptr->seek_ex(p_position, file::seek_from_beginning, *m_abort);
                file_ptr->read(p_buffer, p_size, *m_abort);
            };
            void get(void * p_buffer, t_size p_size = 0) const {
                if (!p_size) {
                    p_size = get_size();
                }
                get(p_buffer, get_position(), p_size);
            };
        };
    };

};

using namespace foobar2000_io;

template<typename T>
class container_matroska_factory_t : public service_factory_t<T> {};

// {32491C6C-F941-42cf-8519-EBF5061BB088}
FOOGUIDDECL const GUID container_matroska::class_guid = 
{ 0x32491c6c, 0xf941, 0x42cf, { 0x85, 0x19, 0xeb, 0xf5, 0x6, 0x1b, 0xb0, 0x88 } };

#endif // _CONTAINER_MATROSKA_H_
