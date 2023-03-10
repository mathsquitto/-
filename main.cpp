#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <queue>
#include <stack>
#include <string>
#include <thread>
#include <vector>
#include <locale.h>

#include <cstdlib>
#include <ctime>
#include <iso646.h>



#if defined(_WIN32) 
#define FS_ROOT "C:\\"
#elif defined(__unix__) or defined(unix) or defined(__unix)
#define FS_ROOT "/"
#else
#error "Unsupported operating system"
#endif

namespace fs = std::filesystem;

void sleep_random_ms(size_t sleep_min, size_t sleep_max)
{
  const std::chrono::milliseconds ms(sleep_min + rand() % sleep_max);
  std::this_thread::sleep_for(ms);
}

void println_thread_safe(std::ostream &out, const std::string &string)
{
  static std::mutex output_lock;
  std::lock_guard<std::mutex> lock_guard(output_lock);
  out << string << std::endl;
}

bool path_get_directory_iterator(const fs::path &path, fs::directory_iterator &it)
{
  try
  {
    it = fs::directory_iterator(path);
    return true;
  }
  catch (fs::filesystem_error &fs_error)
  {
    println_thread_safe(std::cerr, fs_error.what());
  }
  catch (std::exception &error)
  {
    println_thread_safe(std::cerr, error.what());
  }
  return false;
}

bool path_is_regular_directory(const fs::path &path)
{
  try
  {
    return fs::is_directory(path) and not fs::is_symlink(path);
  }
  catch (fs::filesystem_error &fs_error)
  {
    println_thread_safe(std::cerr, fs_error.what());
  }
  catch (std::exception &error)
  {
    println_thread_safe(std::cerr, error.what());
  }
  return false;
}

bool path_is_regular_file(const fs::path &path)
{
  try
  {
    return fs::is_regular_file(path) and not fs::is_symlink(path);
  }
  catch (fs::filesystem_error &fs_error)
  {
    println_thread_safe(std::cerr, fs_error.what());
  }
  catch (std::exception &error)
  {
    println_thread_safe(std::cerr, error.what());
  }
  return false;
}

bool path_filename_equals(const fs::path &path, const std::string &filename)
{
  try
  {
    return path.filename().string() == filename;
  }
  catch (fs::filesystem_error &fs_error)
  {
    println_thread_safe(std::cerr, fs_error.what());
  }
  catch (std::exception &error)
  {
    println_thread_safe(std::cerr, error.what());
  }
  return false;
}

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
      println_thread_safe(std::cerr, fs_error.what());
    }
    catch (std::exception &error)
    {
      println_thread_safe(std::cerr, error.what());
    }
    return false;
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
  FSTree(const fs::path &root_path, size_t threads_max = 10)
      : m_root(root_path), n_threads_max(threads_max), n_threads(0)
  {
    std::queue<FSTreeNode *> dirs;
    FSTreeNode *dir, *child;
    fs::directory_iterator it_dir;

    if (n_threads_max == 0)
      throw std::logic_error("n_threads_max must be at least 1");

    dirs.push(&m_root);
    while (not dirs.empty())
    {
      dir = dirs.front();
      dirs.pop();
      if (path_is_regular_directory(dir->path()) and path_get_directory_iterator(dir->path(), it_dir))
      {
        for (const fs::directory_entry &it : it_dir)
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
      out << dir->path().string() << "/:\n";
      if (dir->empty())
        continue;
      for (const FSTreeNode *const &child : dir->children())
      {
        if (child->ishidden())
          continue;
        out << child->path().filename().string() << '\t';
        if (child->isdir())
          dirs.push(child);
      }
      out << '\n';
      if (not dirs.empty())
        out << '\n';
    }

    return out;
  }

  std::vector<fs::path> find(const std::string &filename, size_t threads_max = 10)
  {
    std::vector<fs::path> results;
    _find(this, &m_root, std::cref(filename), std::ref(results));
    return results;
  }

private:
  FSTreeNode m_root;
  const size_t n_threads_max;
  size_t n_threads;
  std::mutex n_threads_lock;

  void assert_n_threads_valid()
  {
    while (not n_threads_lock.try_lock())
      sleep_random_ms(5, 10);
    if (n_threads > n_threads_max)
      throw std::logic_error("Number of threads is over limit: n_threads == " + std::to_string(n_threads));
    n_threads_lock.unlock();
  }

  static void _find(FSTree *self, const FSTreeNode *root, const std::string &filename, std::vector<fs::path> &results)
  {
    static std::mutex results_lock;
    std::queue<const FSTreeNode *> dirs;
    std::vector<std::thread> threads;
    fs::path dir;

    srand(time(NULL) + std::hash<std::thread::id>{}(std::this_thread::get_id()));

    dirs.push(root);
    while (not dirs.empty())
    {
      auto dir = dirs.front();
      dirs.pop();

      for (auto entry : dir->children())
      {
        if (path_filename_equals(entry->path(), filename))
        {
          std::lock_guard<std::mutex> lock_guard(results_lock);
          results.push_back(entry->path());
        }
        if (entry->isdir())
        {
          std::lock_guard<std::mutex> lock_guard(self->n_threads_lock);
          if (self->n_threads < self->n_threads_max)
          {
            self->n_threads++;
            threads.push_back(std::thread(_find, self, entry, std::cref(filename), std::ref(results)));
          }
          else
            dirs.push(entry);
        }
      }
    }

    for (auto &thread : threads)
    {
      thread.join();
      std::lock_guard<std::mutex> lock_guard(self->n_threads_lock);
      self->n_threads--;
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

bool cstring_represents_integer(const char *str)
{
  const size_t digits_max = 64;
  size_t i = 0;

  if (str[0] == '+' or str[0] == '-')
    i++;

  for (; i < digits_max and str[i] != '\0'; i++)
    if (not isdigit(str[i]))
      return false;
  return true;
}

void usage()
{
  const char *usage_cstr = "Usage: tfind [<OPTIONS>] <filename>\n\t--path\t\t<path from where to start search>\n\t"
                           "--num_threads\t<maximum number of threads>";
  std::cerr << usage_cstr << std::endl;
  exit(1);
}

void process_args(int argc, char *argv[], std::string &filename, fs::path &root, size_t &num_threads)
{
  const size_t option_length_max = 128;
  bool filename_option_found = false;
  bool root_option_found = false;
  bool num_threads_option_found = false;

  for (int i = 1; i < argc; i++)
  {
    if (strncmp(argv[i], "--path", option_length_max) == 0 and not root_option_found)
    {
      if (i + 1 >= argc) 
        usage();
      root = argv[++i];
      root_option_found = true;
    }
    else if (strncmp(argv[i], "--num_threads", option_length_max) == 0 and not num_threads_option_found)
    {
      if (i + 1 >= argc)
        usage();
      if (not cstring_represents_integer(argv[i + 1]))
        usage();
      num_threads = atoll(argv[++i]);
      num_threads_option_found = true;
    }
    else if (not filename_option_found)
    {
      filename = argv[i];
      filename_option_found = true;
    }
    else
      usage();
  }

  if (not filename_option_found)
    usage();
}

int main(int argc, char *argv[])
{
  setlocale(0, "Rus");
  std::string filename;
  fs::path root = FS_ROOT;
  size_t num_threads = 10;
  const size_t num_threads_min = 1;
  const size_t num_threads_max = 200;

  process_args(argc, argv, filename, root, num_threads);
  if (not(num_threads_min <= num_threads and num_threads <= num_threads_max))
  {
    std::cerr << "number of threads must be inside [" + std::to_string(num_threads_min) + "; " +
                     std::to_string(num_threads_max) + "]"
              << std::endl;
    return EXIT_FAILURE;
  }

  FSTree fs_tree(root, num_threads);
  std::vector<fs::path> paths = fs_tree.find(filename);
  std::cerr << "found " << paths.size() << " file(s)" << std::endl;
  std::cout << paths << std::endl;

  return EXIT_SUCCESS;
}
