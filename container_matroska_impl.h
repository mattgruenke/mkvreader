#ifndef _CONTAINER_MATROSKA_IMPL_H_
#define _CONTAINER_MATROSKA_IMPL_H_

#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string/case_conv.hpp>

#include "matroska_parser.h"
#include "container_matroska.h"

typedef boost::shared_ptr<MatroskaParser> matroska_parser_ptr;

class container_matroska_impl : public container_matroska
{
private:
    typedef std::list<matroska::attachment> attachment_list_impl;

    abort_callback * m_abort;
    std::string m_path;
    attachment_list_impl m_attachment_list;

    void cleanup() {
        m_path.clear();
        m_attachment_list.clear();
    };

protected:
    container_matroska_impl() {
    };
    ~container_matroska_impl() {
        cleanup();
    };

public:
    virtual void open(const char * p_path, bool p_info_only, abort_callback & p_abort);
    virtual void open_file(std::fstream & p_out, const std::ios_base::openmode p_mode = std::ios_base::in) const;
    virtual void get_display_path(std::string & p_out) const;
    virtual bool is_our_path(const char * p_path) const {
        std::string ext = boost::algorithm::to_lower_copy(
            boost::filesystem::path(p_path).extension().string() );
        return (ext == "mka" || ext == "mkv");
    }
    virtual const matroska::attachment_list * get_attachment_list() const;
};


#endif // _CONTAINER_MATROSKA_IMPL_H_
