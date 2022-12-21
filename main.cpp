#include <filesystem>
#include <iostream>
#include <thread>
#include <mutex>
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
  FSTree(const fs::path &root_path, size_t threads_max=10) : m_root(root_path), n_threads(0), n_threads_max(threads_max)
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
        if (get_directory_iterator(dir, it_dir))
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

  std::vector<fs::path> find(const std::string &filename, size_t threads_max=10)
  {
    std::vector<fs::path> results;
    FSTree::_find(this, &m_root, filename, results);
    return results;
  }

private:
  FSTreeNode m_root;
  const size_t n_threads_max;
  size_t n_threads;
  std::mutex n_threads_lock;
  std::mutex output_lock;


  bool get_directory_iterator(const FSTreeNode * tree_node, fs::directory_iterator & it)
  {
    try
    {
      it = fs::directory_iterator(tree_node->path());
      return true;
    }
    catch (fs::filesystem_error & fs_error)
    {
      println_thread_safe(std::cerr, fs_error.what());
    }
    catch (std::exception & error)
    {
      println_thread_safe(std::cerr, error.what());
    }
    return false;
  }

  bool should_create_threads(size_t threads=1)
  {
    bool should_create = false;

    while (not n_threads_lock.try_lock())
      sleep_random_ms(5, 10);
    if (n_threads + threads <= n_threads_max)
      should_create = true;

    return should_create;
  }

  void n_threads_increment()
  {
    while (not n_threads_lock.try_lock())
      sleep_random_ms(5, 10);
    n_threads++;
    n_threads_lock.unlock();
  }

  void n_threads_decrement()
  {
    while (not n_threads_lock.try_lock())
      sleep_random_ms(5, 10);
    n_threads--;
    n_threads_lock.unlock();
  }

  size_t get_n_threads() const
  {
    return n_threads;
  }

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
    catch (std::exception & error)
    {
      println_thread_safe(std::cerr, error.what());
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
    catch (std::exception & error)
    {
      println_thread_safe(std::cerr, error.what());
    }
    return false;
  }

  bool path_filename_equals(const fs::path & path, const std::string & filename)
  {
    try
    {
      return path.filename().string() == filename;
    }
    catch (fs::filesystem_error & fs_error)
    {
      println_thread_safe(std::cerr, fs_error.what());
    }
    catch (std::exception & error)
    {
      println_thread_safe(std::cerr, error.what());
    }
    return false;
  }

  void sleep_random_ms(size_t sleep_min, size_t sleep_max)
  {
    const std::chrono::milliseconds ms(sleep_min + rand() % sleep_max);
    std::this_thread::sleep_for(ms);
  }

  static void _find(
      FSTree * self,
      const FSTreeNode * root,
      const std::string & filename,
      std::vector<fs::path> & results)
  {
    std::queue<FSTreeNode *> dirs;
    fs::path dir;
    std::vector<std::thread> children;

    srand(time(NULL) + std::hash<std::thread::id>{}(std::this_thread::get_id()));

    dirs.push(&self->m_root);
    while (not dirs.empty())
    {
      auto dir = dirs.front();
      dirs.pop();

      if (self->get_n_threads() > self->n_threads_max)
        throw std::logic_error("number of threads is over limit");

      for (auto entry : dir->children())
      {
        if (self->path_filename_equals(entry->path().filename(), filename))
          results.push_back(entry->path());
        if (entry->isdir())
        {
          if (not self->should_create_threads())
            dirs.push(entry);
          else
          {
            self->n_threads_increment();
            children.push_back(std::thread(FSTree::_find, self, entry, filename, results));
          }
          self->n_threads_lock.unlock();
        }
      }
    }

    for (size_t i = 0; i < children.size(); i++)
    {
      children[i].join();
      self->n_threads_decrement();
    }
  }
};


std::ostream &operator<<(std::ostream &out, const std::vector<fs::path> vector)
{
  size_t i = 0;
  for (i = 0; i + 1 < vector.size(); i++)
    out << vector[i].string() << "\n";
  if (i < vector.size())
    out << vector[i].string();
  return out;
}

bool args_contain_option(int argc, char *argv[], const char *option)
{
  const size_t option_max = 32;

  for (size_t i = 0; i < argc; i++)
    if (strncmp(option, argv[i], option_max) == 0)
      return true;
  return false;
}

char *args_get_option_argument(int argc, char *argv[], const char *option)
{
  const size_t option_max = 32;

  for (size_t i = 0; i + 1 < argc; i++)
    if (strncmp(option, argv[i], option_max) == 0)
      return argv[i + 1];

  return nullptr;
}

bool cstring_represents_integer(const char *str)
{
  const size_t digits_max = 64;
  size_t i = 0;

  if (str[0] == '+' or str[0] == '-')
    i++;

  for (; i < digits_max && str[i] != '\0'; i++)
    if (not isdigit(str[i]))
      return false;
  return true;
}

void process_args(int argc, char *argv[], std::string &filename, fs::path &root, size_t &num_threads)
{
  const size_t num_threads_min = 0;
  const size_t num_threads_max = 200;
  const char *usage_cstr = "Usage: ./main <filename> [--path <path>] [--num_threads <number>]";
  const std::string invalid_num_threads_str = "Inavlid --num_threads argument, must be between " +
                                              std::to_string(num_threads_min) + " and " +
                                              std::to_string(num_threads_max);
  char *path_cstr;
  char *num_threads_cstr;

  if (argc == 1)
  {
    std::cerr << usage_cstr << std::endl;
    exit(1);
  }
  if (argc == 2)
  {
    filename = argv[1];
    return;
  }

  filename = argv[1];
  if (args_contain_option(argc - 2, argv + 2, "--path"))
  {
    if ((path_cstr = args_get_option_argument(argc, argv, "--path")) != nullptr)
      root = path_cstr;
    else
    {
      std::cerr << usage_cstr << std::endl;
      exit(1);
    }
  }
  if (args_contain_option(argc - 2, argv + 2, "--num_threads"))
  {
    if ((num_threads_cstr = args_get_option_argument(argc, argv, "--num_threads")) != nullptr and
        cstring_represents_integer(num_threads_cstr))
    {
      num_threads = atoll(num_threads_cstr);
      if (not(num_threads_min <= num_threads and num_threads <= num_threads_max))
      {
        std::cerr << invalid_num_threads_str << std::endl;
        exit(1);
      }
    }
    else if (num_threads_cstr == nullptr)
    {
      std::cerr << usage_cstr << std::endl;
      exit(1);
    }
    else
    {
      std::cerr << invalid_num_threads_str << std::endl;
      exit(1);
    }
  }
}


int main(int argc, char *argv[])
{
  fs::path root = "C:\\";
  std::string filename;
  size_t num_threads = 0;

  process_args(argc, argv, filename, root, num_threads);
  FSTree fs_tree(root);
  std::vector<fs::path> paths = fs_tree.find(filename);
  std::cerr << "found " << paths.size() << " file(s)" << std::endl;
  std::cout << paths << std::endl;

  return EXIT_SUCCESS;
}
