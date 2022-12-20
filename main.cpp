#include <filesystem>
#include <iostream>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <queue>
#include <stack>
#include <string>
#include <vector>
#include <chrono>

#include <iso646.h>
#include <cstdlib>
#include <ctime>


namespace fs = std::filesystem;



class FSTreeNode final
{
public:
  FSTreeNode(const fs::path &path) : m_path(path)
  {
  }

  void append_child(FSTreeNode *child)
  {
    m_children.push_back(child);
  }

  FSTreeNode *pop_child()
  {
    FSTreeNode *back = m_children.back();
    m_children.pop_back();
    return back;
  }

  bool isdir() const
  {
    try
    {
      return fs::is_directory(m_path) and not fs::is_symlink(m_path);
    }
    catch (fs::filesystem_error &fs_error)
    {
      std::cerr << fs_error.what() << std::endl;
    }
    catch (std::exception &error)
    {
      std::cerr << error.what() << std::endl;
    }
  }

  bool ishidden() const
  {
    return m_path.string().find("/.") != std::string::npos;
  }

  bool empty() const
  {
    return m_children.empty();
  }

  const fs::path &path() const
  {
    return m_path;
  }

  const std::vector<FSTreeNode *> children() const
  {
    return m_children;
  }

  FSTreeNode *operator[](size_t i) const
  {
    return m_children[i];
  }

  void clear()
  {
    m_children.clear();
  }

private:
  const fs::path m_path;
  std::vector<FSTreeNode *> m_children;
};

class FSTree final
{
public:
  FSTree(const fs::path &root_path, size_t threads_max=10) : m_root(root_path)
  {
    std::queue<FSTreeNode *> dirs;
    FSTreeNode *dir, *child;
    fs::directory_iterator it_dir;

    dirs.push(&m_root);
    while (not dirs.empty())
    {
      dir = dirs.front();
      dirs.pop();
      if (path_is_regular_directory(dir->path()))
      {
        try
        {
          it_dir = fs::directory_iterator(dir->path());
        }
        catch (fs::filesystem_error &fs_error)
        {
          std::cerr << fs_error.what() << '\n';
          continue;
        }
        for (auto &it : it_dir)
        {
          child = new FSTreeNode(it);
          dir->append_child(child);
          if (child->isdir())
            dirs.push(child);
        }
      }
    }
  }

  ~FSTree()
  {
    if (not m_root.empty())
      free();
  }

  void free()
  {
    std::stack<FSTreeNode *> dirs;

    for (FSTreeNode *it : m_root.children())
      dirs.push(it);

    m_root.clear();
    while (not dirs.empty())
    {
      FSTreeNode *&dir = dirs.top();
      if (not dir->empty())
      {
        do
          dirs.push(dir->pop_child());
        while (not dir->empty());
      }
      else
      {
        dirs.pop();
        delete dir;
      }
    }
  }

  friend std::ostream &operator<<(std::ostream &out, const FSTree &self)
  {
    std::queue<const FSTreeNode *> dirs;
    const FSTreeNode *dir;
    fs::directory_iterator it_dir;

    dirs.push(&self.m_root);
    while (not dirs.empty())
    {
      dir = dirs.front();
      dirs.pop();
      if (dir->empty())
        continue;
      out << dir->path().string() << "/:\n";
      for (auto &child : dir->children())
      {
        if (child->ishidden())
          continue;
        out << '\t' << child->path().filename().string() << '\n';
        if (child->isdir())
          dirs.push(child);
      }
      if (dirs.size() != 0)
        out << '\n';
    }

    return out;
  }

private:
  FSTreeNode m_root;
  std::mutex output_lock;


  void println_thread_safe(std::ostream & out, const std::string & string)
  {
    while (not output_lock.try_lock())
      sleep_random_ms(5, 10);
    out << string << std::endl;
    output_lock.unlock();
  }

  bool path_is_regular_directory(const fs::path & path)
  {
    try
    {
      return fs::is_directory(path) and not fs::is_symlink(path);
    }
    catch (fs::filesystem_error & fs_error)
    {
      println_thread_safe(std::cerr, fs_error.what());
    }
    return false;
  }

  bool path_is_regular_file(const fs::path & path)
  {
    try
    {
      return fs::is_regular_file(path) and not fs::is_symlink(path);
    }
    catch (fs::filesystem_error & fs_error)
    {
      println_thread_safe(std::cerr, fs_error.what());
    }
    return false;
  }

  void sleep_random_ms(size_t sleep_min, size_t sleep_max)
  {
    const std::chrono::milliseconds ms(sleep_min + rand() % sleep_max);
    std::this_thread::sleep_for(ms);
  }
};


int main()
{
  fs::path root = ".";

  FSTree fs_tree(root);
  std::cout << fs_tree << std::endl;

  return EXIT_SUCCESS;
}
