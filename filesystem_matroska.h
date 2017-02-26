#ifndef _FOO_INPUT_MATROSKA_FILESYSTEM_MATROSKA_H_
#define _FOO_INPUT_MATROSKA_FILESYSTEM_MATROSKA_H_

#include <fstream>

#include "container_matroska.h"


class filesystem_matroska /* : public filesystem */ {
public:
	virtual bool get_canonical_path(const char * p_path, std::string & p_out);
	virtual bool is_our_path(const char * p_path);
	virtual bool get_display_path(const char * p_path, std::string & p_out);

	virtual void open(std::fstream & p_out,const char * p_path, std::ios_base::openmode p_mode, abort_callback & p_abort);
    virtual void remove(const char * p_path,abort_callback & p_abort);
    virtual void move(const char * p_src,const char * p_dst,abort_callback & p_abort);
	//! Queries whether a file at specified path belonging to this filesystem is a remove object or not.
	virtual bool is_remote(const char * p_src);

	//! Retrieves stats of a file at specified path.
//	virtual void get_stats(const char * p_path, t_filestats & p_stats, bool & p_is_writeable,abort_callback & p_abort);
	
	virtual bool relative_path_create(const char * file_path,const char * playlist_path, std::string & out) {return 0;}
	virtual bool relative_path_parse(const char * relative_path,const char * playlist_path, std::string & out) {return 0;}

	//! Creates a directory.
//	virtual void create_directory(const char * p_path, abort_callback & p_abort);

//	virtual void list_directory(const char * p_path, directory_callback & p_out, abort_callback & p_abort);

	//! Hint; returns whether this filesystem supports mime types.
	virtual bool supports_content_types();

private:
/*
    inline bool get_source_file_path(const char * p_path, std::string & p_out)
    {
        std::string path, file;
        if (archive_impl::g_parse_unpack_path(p_path, path, file)) {
            path << "|" << file;
        } else {
            path = p_path;
        }
        path.remove_chars(0, 11);
        path.truncate(path.find_last('|'));
        std::string ext(pfc::string_extension(path).get_ptr());
        if (filesystem::g_is_recognized_path(path)) {
            p_out = path;
            return true;
        }
        return false;
    }

    inline bool get_attachment_file_name(const char * p_path, std::string & p_out)
    {
        std::string path, file;
        if (archive_impl::g_parse_unpack_path(p_path, path, file)) {
            path << "|" << file;
        } else {
            path = p_path;
        }
        path.remove_chars(0, 11);
        if (path.find_last('|') != infinite) {
            path.remove_chars(0, path.find_last('|')+1);
        } else {
            path.remove_chars(0, path.find_last('\\')+1);
        }
        if (path.find_first('\\') == 0) {
            path.remove_chars(0, 1);
        }
        if (path.length() != 0) {
            p_out = path;
            return true;
        }
        return false;
    }
*/

public:
    static void g_make_matroska_path(std::string & path, const char * file, const char * name)
    {
	    path = "matroska://";
	    path += file;
	    path += "|";
	    path += name;
    }
};


#endif//_FOO_INPUT_MATROSKA_FILESYSTEM_MATROSKA_H_
