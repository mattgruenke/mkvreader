#include "container_matroska_impl.h"

/**
 * container_matroska
 */

void container_matroska_impl::open(const char * p_path, bool p_info_only, abort_callback &p_abort) {
    cleanup();
    if (!is_our_path(p_path)) {
//        throw exception_io_unsupported_format();  TO_DO
    }
    m_path = p_path;
    m_abort = &p_abort;
//    std::ifstream file(m_path.c_str());
//    if (!file) throw exception_io_unsupported_format();   TO_DO
    try {
        matroska_parser_ptr parser = matroska_parser_ptr(new MatroskaParser(p_path, *m_abort));
        parser->Parse(p_info_only);
        MatroskaParser::attachment_list attachments = parser->GetAttachmentList();
        for (MatroskaParser::attachment_list::iterator item = attachments.begin(); item != attachments.end(); ++item) {
            matroska::attachment attachment(this, *m_abort, item->FileName.GetUTF8().c_str(), item->MimeType.c_str(), item->Description.GetUTF8().c_str(),
                static_cast<size_t>(item->SourceDataLength), static_cast<size_t>(item->SourceStartPos));
            m_attachment_list.push_back(attachment);
        }
    } catch (...) {
//        throw exception_io_unsupported_format();  TO_DO
    }
}

void container_matroska_impl::open_file(std::fstream & p_out, const std::ios_base::openmode p_mode) const {
    p_out.open(m_path.c_str(), p_mode);
//    if (!p_out) m_abort();    TO_DO
}

void container_matroska_impl::get_display_path(std::string & p_out) const {
    p_out = m_path;
}

const matroska::attachment_list * container_matroska_impl::get_attachment_list() const {
    return reinterpret_cast<const matroska::attachment_list *>(&m_attachment_list);
}
