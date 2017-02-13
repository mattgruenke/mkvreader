#ifndef _CONTAINER_MATROSKA_H_
#define _CONTAINER_MATROSKA_H_

#include <list>
#include <string>
#include <fstream>

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
        virtual void open_file(std::fstream &p_out, const std::ios_base::openmode p_mode = std::ios_base::in) const =0;
        virtual void get_display_path(std::string & p_out) const =0;
        virtual bool is_our_path(const char * p_path) const =0;
        virtual const matroska::attachment_list * get_attachment_list() const =0;

    public:
        static void g_open(boost::shared_ptr<container_matroska> & p_out, const char * p_path, bool p_info_only, abort_callback & p_abort) {
#if 0  // TO_DO
            service_enum_t<container_matroska> e;
            boost::shared_ptr<container_matroska> ptr;
            while(e.next(ptr)) {
                if (ptr->is_our_path(p_path)) {
		            ptr->open(p_path, p_info_only, p_abort);
                    p_out = ptr;
		            return;
                }
            }
	        throw exception_io_unsupported_format();
#endif
        }
        static bool g_is_our_path(const char * p_path) {
#if 0  // TO_DO
            service_enum_t<container_matroska> e;
            boost::shared_ptr<container_matroska> ptr;
            while(e.next(ptr)) {
                if (ptr->is_our_path(p_path)) {
		            return true;
                }
            }
#endif
            return false;
        }
    };

    typedef boost::shared_ptr<container_matroska> container_matroska_ptr;

    namespace matroska {
        class attachment {
        private:
            container_matroska * m_owner;
            abort_callback * m_abort;
            std::string m_name, m_mime_type, m_description;
            size_t m_size;
            size_t m_position;

        public:
            attachment() : m_owner(0), m_abort(0) {};
            attachment(container_matroska *p_owner, abort_callback & p_abort, const char * p_name, const char * p_mime_type,
                        const char * p_description, const size_t p_size, const size_t p_position)
                : m_name(p_name), m_mime_type(p_mime_type), m_description(p_description), m_size(p_size), m_position(p_position)
            {
                m_owner = p_owner;
                m_abort = &p_abort;
            };
            ~attachment() {};
            void get_name(std::string & p_out) const { p_out = m_name; };
            void get_mime_type(std::string & p_out) const { p_out = m_mime_type; };
            void get_description(std::string & p_out) const { p_out = m_description; };
            size_t get_size() const { return m_size; };
            size_t get_position() const { return m_position; };
            void get(void * p_buffer, size_t p_position, size_t p_size) const {
                std::fstream file;
                m_owner->open_file(file);
                file.seekp(p_position);
                // if (file) m_abort();     TO_DO
                file.read(static_cast<char *>(p_buffer), p_size);
                // if (file) m_abort();     TO_DO
            };
            void get(void * p_buffer, size_t p_size = 0) const {
                if (!p_size) {
                    p_size = get_size();
                }
                get(p_buffer, get_position(), p_size);
            };
        };
    };

};

using namespace foobar2000_io;

#endif // _CONTAINER_MATROSKA_H_
