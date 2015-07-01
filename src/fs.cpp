/*
  The MIT License (MIT)

  Copyright (c) 2014 Antonio SJ Musumeci <trapexit@spawn.link>

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

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstdlib>
#include <iterator>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <glob.h>

#include "fs.hpp"
#include "xattr.hpp"
#include "str.hpp"

using std::string;
using std::vector;
using std::map;
using std::istringstream;

template <typename Iter>
Iter
random_element(Iter begin,
               Iter end)
{
  const unsigned long n = std::distance(begin, end);

  std::advance(begin, (std::rand() % n));

  return begin;
}

namespace fs
{
  namespace path
  {
    string
    dirname(const string &path)
    {
      string parent = path;
      string::reverse_iterator i;
      string::reverse_iterator bi;

      bi = parent.rend();
      i  = parent.rbegin();
      while(*i == '/' && i != bi)
        i++;

      while(*i != '/' && i != bi)
        i++;

      while(*i == '/' && i != bi)
        i++;

      parent.erase(i.base(),parent.end());

      return parent;
    }

    string
    basename(const string &path)
    {
      return path.substr(path.find_last_of('/')+1);
    }

    bool
    is_empty(const string &path)
    {
      DIR           *dir;
      struct dirent *de;

      dir = ::opendir(path.c_str());
      if(!dir)
        return false;

      while((de = ::readdir(dir)))
        {
          const char *d_name = de->d_name;

          if(d_name[0] == '.'     &&
             ((d_name[1] == '\0') ||
              (d_name[1] == '.' && d_name[2] == '\0')))
            continue;

          ::closedir(dir);

          return false;
        }

      ::closedir(dir);

      return true;
    }

    bool
    exists(const vector<string> &paths,
           const string         &fusepath)
    {
      for(size_t i = 0, ei = paths.size(); i != ei; i++)
        {
          int rv;
          string path;
          struct stat st;

          path = fs::path::make(paths[i],fusepath);

          rv  = ::lstat(path.c_str(),&st);
          if(rv == 0)
            return true;
        }

      return false;
    }
  }

  void
  findallfiles(const vector<string> &srcmounts,
               const string         &fusepath,
               vector<string>       &paths)
  {
    for(size_t i = 0, ei = srcmounts.size(); i != ei; i++)
      {
        int rv;
        string fullpath;
        struct stat st;

        fullpath = fs::path::make(srcmounts[i],fusepath);

        rv = ::lstat(fullpath.c_str(),&st);
        if(rv == 0)
          paths.push_back(fullpath);
      }
  }

  int
  listxattr(const string &path,
            vector<char> &attrs)
  {
#ifndef WITHOUT_XATTR
    int rv;
    int size;

    rv    = -1;
    errno = ERANGE;
    while(rv == -1 && errno == ERANGE)
      {
        size = ::listxattr(path.c_str(),NULL,0);
        attrs.resize(size);
        rv   = ::listxattr(path.c_str(),&attrs[0],size);
      }

    return rv;
#else
    errno = ENOTSUP;
    return -1;
#endif
  }

  int
  listxattr(const string   &path,
            vector<string> &attrvector)
  {
    int rv;
    vector<char> attrs;

    rv = listxattr(path,attrs);
    if(rv != -1)
      {
        string tmp(attrs.begin(),attrs.end());
        str::split(attrvector,tmp,'\0');
      }

    return rv;
  }

  int
  listxattr(const string &path,
            string       &attrstr)
  {
    int rv;
    vector<char> attrs;

    rv = listxattr(path,attrs);
    if(rv != -1)
      attrstr = string(attrs.begin(),attrs.end());

    return rv;
  }

  int
  getxattr(const string &path,
           const string &attr,
           vector<char> &value)
  {
#ifndef WITHOUT_XATTR
    int rv;
    int size;

    rv    = -1;
    errno = ERANGE;
    while(rv == -1 && errno == ERANGE)
      {
        size = ::getxattr(path.c_str(),attr.c_str(),NULL,0);
        value.resize(size);
        rv   = ::getxattr(path.c_str(),attr.c_str(),&value[0],size);
      }

    return rv;
#else
    errno = ENOTSUP;
    return -1;
#endif
  }

  int
  getxattr(const string &path,
           const string &attr,
           string       &value)
  {
    int          rv;
    vector<char> tmpvalue;

    rv = getxattr(path,attr,tmpvalue);
    if(rv != -1)
      value = string(tmpvalue.begin(),tmpvalue.end());

    return rv;
  }


  int
  getxattrs(const string       &path,
            map<string,string> &attrs)
  {
    int rv;
    string attrstr;

    rv = listxattr(path,attrstr);
    if(rv == -1)
      return -1;

    {
      string key;
      istringstream ss(attrstr);

      while(getline(ss,key,'\0'))
        {
          string value;

          rv = getxattr(path,key,value);
          if(rv != -1)
            attrs[key] = value;
        }
    }

    return 0;
  }

  int
  setxattr(const string &path,
           const string &key,
           const string &value,
           const int     flags)
  {
#ifndef WITHOUT_XATTR
    return ::setxattr(path.c_str(),
                      key.c_str(),
                      value.data(),
                      value.size(),
                      flags);
#else
    errno = ENOTSUP;
    return -1;
#endif
  }

  int
  setxattr(const int     fd,
           const string &key,
           const string &value,
           const int     flags)
  {
#ifndef WITHOUT_XATTR
    return ::fsetxattr(fd,
                       key.c_str(),
                       value.data(),
                       value.size(),
                       flags);
#else
    errno = ENOTSUP;
    return -1;
#endif
  }

  int
  setxattrs(const string             &path,
            const map<string,string> &attrs)
  {
    int fd;

    fd = ::open(path.c_str(),O_RDONLY|O_NONBLOCK);
    if(fd == -1)
      return -1;

    for(map<string,string>::const_iterator
          i = attrs.begin(), ei = attrs.end(); i != ei; ++i)
      {
        setxattr(fd,i->first,i->second,0);
      }

    return ::close(fd);
  }

  int
  copyxattrs(const string &from,
             const string &to)
  {
    int rv;
    map<string,string> attrs;

    rv = getxattrs(from,attrs);
    if(rv == -1)
      return -1;

    return setxattrs(to,attrs);
  }

  static
  int
  get_fs_ioc_flags(const string &file,
                   int          &flags)
  {
    int fd;
    int rv;
    const int openflags = O_RDONLY|O_NONBLOCK;

    fd = ::open(file.c_str(),openflags);
    if(fd == -1)
      return -1;

    rv = ::ioctl(fd,FS_IOC_GETFLAGS,&flags);
    if(rv == -1)
      {
        int error = errno;
        ::close(fd);
        errno = error;
        return -1;
      }

    return ::close(fd);
  }

  static
  int
  set_fs_ioc_flags(const string &file,
                   const int     flags)
  {
    int fd;
    int rv;
    const int openflags = O_RDONLY|O_NONBLOCK;

    fd = ::open(file.c_str(),openflags);
    if(fd == -1)
      return -1;

    rv = ::ioctl(fd,FS_IOC_SETFLAGS,&flags);
    if(rv == -1)
      {
        int error = errno;
        ::close(fd);
        errno = error;
        return -1;
      }

    return ::close(fd);
  }

  int
  copyattr(const string &from,
           const string &to)
  {
    int rv;
    int flags;

    rv = get_fs_ioc_flags(from,flags);
    if(rv == -1)
      return -1;

    return set_fs_ioc_flags(to,flags);
  }

  int
  clonepath(const string &fromsrc,
            const string &tosrc,
            const string &relative)
  {
    int         rv;
    struct stat st;
    string      topath;
    string      frompath;
    string      dirname;

    dirname = fs::path::dirname(relative);
    if(!dirname.empty())
      {
        rv = clonepath(fromsrc,tosrc,dirname);
        if(rv == -1)
          return -1;
      }

    frompath = fs::path::make(fromsrc,relative);
    rv = ::stat(frompath.c_str(),&st);
    if(rv == -1)
      return -1;
    else if(!S_ISDIR(st.st_mode))
      return (errno = ENOTDIR,-1);

    topath = fs::path::make(tosrc,relative);
    rv = ::mkdir(topath.c_str(),st.st_mode);
    if(rv == -1)
      {
        if(errno != EEXIST)
          return -1;

        rv = ::chmod(topath.c_str(),st.st_mode);
        if(rv == -1)
          return -1;
      }

    rv = ::chown(topath.c_str(),st.st_uid,st.st_gid);
    if(rv == -1)
      return -1;

    // It may not support it... it's fine...
    rv = copyattr(frompath,topath);
    if(rv == -1 && errno != ENOTTY)
      return -1;

    rv = copyxattrs(frompath,topath);
    if(rv == -1 && errno != ENOTTY)
      return -1;

    return 0;
  }

  void
  glob(const vector<string> &patterns,
       vector<string>       &strs)
  {
    int flags;
    glob_t gbuf;

    flags = 0;
    for(size_t i = 0; i < patterns.size(); i++)
      {
        glob(patterns[i].c_str(),flags,NULL,&gbuf);
        flags = GLOB_APPEND;
      }

    for(size_t i = 0; i < gbuf.gl_pathc; ++i)
      strs.push_back(gbuf.gl_pathv[i]);

    globfree(&gbuf);
  }
};
