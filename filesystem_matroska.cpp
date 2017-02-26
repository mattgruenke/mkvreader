#include "filesystem_matroska.h"

#if 0
bool filesystem_matroska::get_canonical_path(const char * p_path,pfc::string_base & p_out)
{
	if (is_our_path(p_path)) {
		pfc::string8 source, attachment, canonical_path;
		if (get_source_file_path(p_path, source) && get_attachment_file_name(p_path, attachment)) {
			g_get_canonical_path(source, canonical_path);
			g_make_matroska_path(p_out, canonical_path, attachment);
			return true;
		}
	}
	return false;
}

bool filesystem_matroska::is_our_path(const char * p_path)
{
    if (stricmp_utf8_partial(p_path, "matroska://") == 0 || pfc::string_find_last(p_path, '|') != infinite) {
        pfc::string8 source_file;
        get_source_file_path(p_path, source_file);
        pfc::string8 ext(pfc::string_extension(source_file).get_ptr());
        if (stricmp_utf8(ext, "mka") == 0 || stricmp_utf8(ext, "mkv") == 0) {
            return true;
        }
    }
	return false;
}

bool filesystem_matroska::get_display_path(const char * p_path,pfc::string_base & p_out)
{
	pfc::string8 source, attachment;
	if (get_source_file_path(p_path, source) && get_attachment_file_name(p_path, attachment)) {
		g_get_display_path(source, p_out);
		p_out.add_string("|");
		p_out.add_string(attachment);
		return true;
	}
	return false;
}

void filesystem_matroska::open(service_ptr_t<file> & p_out,const char * p_path, t_open_mode p_mode,abort_callback & p_abort)
{
    pfc::string8 path, attachment_file_name;
    if (!get_source_file_path(p_path, path) || !get_attachment_file_name(p_path, attachment_file_name)) {
        throw exception_io_not_found();
    }
    service_ptr_t<container_matroska> matroska;
    container_matroska::g_open(matroska, path, true, p_abort);
    if (matroska == NULL) {
        throw exception_io_not_found();
    }
    for (t_size i = 0; i != matroska->get_attachment_list()->get_count(); ++i) {
        pfc::string8 name;
        matroska::attachment attachment_file = matroska->get_attachment_list()->get_item(i);
        attachment_file.get_name(name);
        if (stricmp_utf8(name, attachment_file_name) == 0) {
            pfc::array_t<unsigned char> buffer;
            buffer.set_size(attachment_file.get_size());
            attachment_file.get(static_cast<void *>(buffer.get_ptr()));
            filesystem::g_open_tempmem(p_out, p_abort);
            p_out->write(static_cast<void *>(buffer.get_ptr()), buffer.get_size(), p_abort);
            p_out->reopen(p_abort);
            return;
        }
    }
    throw exception_io_not_found();
}

void filesystem_matroska::remove(const char * p_path,abort_callback & p_abort)
{
    throw exception_io_denied();
}

void filesystem_matroska::move(const char * p_src,const char * p_dst,abort_callback & p_abort)
{
    throw exception_io_denied();
}

bool filesystem_matroska::is_remote(const char * p_src)
{
    pfc::string8 path;
    if (get_source_file_path(p_src, path)) {
        return g_is_remote(path);
    }
    return false;
}

void filesystem_matroska::get_stats(const char * p_path,t_filestats & p_stats,bool & p_is_writeable,abort_callback & p_abort)
{
    pfc::string8 path;
    if (get_source_file_path(p_path, path)) {
        t_filestats filestats;
        bool is_writeable = false;
        g_get_stats(path, filestats, is_writeable, p_abort);
        pfc::string8 attachment_file_name;
        service_ptr_t<container_matroska> matroska;
        container_matroska::g_open(matroska, path, true, p_abort);
        if (matroska != NULL && get_attachment_file_name(p_path, attachment_file_name)) {
            for (t_size i = 0; i != matroska->get_attachment_list()->get_count(); ++i) {
                pfc::string8 name;
                matroska->get_attachment_list()->get_item(i).get_name(name);
                if (stricmp_utf8(name, attachment_file_name) == 0) {
                    p_stats = filestats;
                    p_stats.m_size = matroska->get_attachment_list()->get_item(i).get_size();
                    p_is_writeable = false;
                    return;
                }
            }
        }
    }
    throw exception_io_not_found();
}

void filesystem_matroska::create_directory(const char * p_path,abort_callback & p_abort)
{
    throw exception_io_denied();
}

void filesystem_matroska::list_directory(const char * p_path,directory_callback & p_out,abort_callback & p_abort)
{
    pfc::string8 path;
    if (get_source_file_path(p_path, path)) {
        service_ptr_t<container_matroska> matroska;
        container_matroska::g_open(matroska, path, true, p_abort);
        if (matroska != NULL) {
            for (t_size i = 0; i != matroska->get_attachment_list()->get_count(); ++i) {
                pfc::string8 name;
                matroska->get_attachment_list()->get_item(i).get_name(name);
                t_filestats filestats;
                bool is_writeable = false;
                g_get_stats(path, filestats, is_writeable, p_abort);
                filestats.m_size = matroska->get_attachment_list()->get_item(i).get_size();
                pfc::string8 url;
                g_make_matroska_path(url, path, name);
                p_out.on_entry(this, p_abort, url, false, filestats);
            }
            return;
        }
    }
    throw exception_io_denied();
}

bool filesystem_matroska::supports_content_types()
{
    return false;
}
#endif
