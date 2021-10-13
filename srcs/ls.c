#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

#define HALF_YEAR_SECOND (365 * 24 * 60 * 60 / 2)

#define MINORBITS        20
#define MINORMASK        ((1U << MINORBITS) - 1)
#define MAJOR(dev)        ((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)        ((unsigned int) ((dev) & MINORMASK))

#ifndef S_IXUGO
#define S_IXUGO (S_IXUSR | S_IXGRP | S_IXOTH)
#endif

/**
 * 隠しファイルの表示方針
 */
enum {
  FILTER_DEFAULT, /**< '.'から始まるもの以外を表示する */
  FILTER_ALMOST,  /**< '.'と'..'以外を表示する */
  FILTER_ALL,     /**< すべて表示する */
};

/**
 * 再帰呼び出しのためのディレクトリ名を保持するリンクリスト
 */
struct dir_path {
  char path[PATH_MAX + 1];
  int depth;
  struct dir_path *next;
};

/**
 * ファイル情報の格納
 */
struct info {
  char name[NAME_MAX + 1];
  char link[PATH_MAX + 1];
  struct stat stat;
  mode_t link_mode;
  bool link_ok;
};

/**
 * ファイル情報を格納する可変長リスト
 */
struct info_list {
  struct info **array;
  int size;
  int used;
};

static void *xmalloc(size_t n);
static void *xrealloc(void *ptr, size_t size);
static struct dir_path *parse_cmd_args(int argc, char**argv);
static void get_mode_string(mode_t mode, char *str);
static void print_type_indicator(mode_t mode);
static void print_user(uid_t uid);
static void print_group(gid_t gid);
static void get_time_string(char *str, time_t time);
static void print_name_with_color(const char *name, mode_t mode, bool link_ok);
static struct dir_path *new_dir_path(const char *path, int depth, struct dir_path *next);
static void init_info_list(struct info_list *list, int size);
static void free_info_list(struct info_list *list);
static void add_info(struct info_list *list, struct info *info);
static struct info *new_info(const char *path, const char *name);
static int compare_name(const void *a, const void *b);
static int compare_name_file_dir(const void *a, const void *b);
static void sort_list(struct info_list *list);
static void sort_argv(int argc, char **argv, bool error_print);
static void print_info(struct info *info);
static const char *find_filename(const char *path);
static void list_dir(struct dir_path *base);

/**
 * 隠しファイルの表示方針
 */
static int filter = FILTER_DEFAULT;
/**
 * 色付き表示する
 */
static bool color = false;
/**
 * 属性を示す文字を表示する
 */
static bool classify = false;
/**
 * ロングフォーマットで表示する
 */
static bool long_format = false;
/**
 * total block size
 */
static unsigned int block_size = 0;
/**
 * 半年前のUNIX時間
 */
static time_t half_year_ago;
/**
 * 再帰的な表示
 */
static bool recursive = false;
/**
 * 修正時間でソートする
 */
static bool time_sort = false;
/**
 * 最初の表示かどうか
 */
static bool first = true;
/**
 * reverseするかどうか
 */
static bool reverse = false;


/**
 * @brief malloc結果がNULLだった場合にexitする。
 * @param[IN] size 確保サイズ
 * @retrun 確保された領域へのポインタ
 */
static void *xmalloc(size_t n) {
  void *p = malloc(n);
  if (p == NULL) {
    perror("");
    exit(EXIT_FAILURE);
  }
  return p;
}

/**
 * @brief realloc結果がNULLだった場合にexitする。
 * @param[IN] ptr 拡張する領域ポインタ
 * @param[IN] originalLength 元のサイズ
 * @param[IN] newLength 確保サイズ
 * @retrun 確保された領域へのポインタ
 */
void *ft_realloc(void *ptr, size_t originalLength, size_t newLength)
{
   if (newLength == 0)
   {
      free(ptr);
      return NULL;
   }
   else if (!ptr)
   {
      return malloc(newLength);
   }
   else if (newLength <= originalLength)
   {
      return ptr;
   }
   else
   {
      // assert((ptr) && (newLength > originalLength));
      void *ptrNew = malloc(newLength);
      if (ptrNew)
      {
          memcpy(ptrNew, ptr, originalLength);
          free(ptr);
      }
      return ptrNew;
    }
}

static void *xrealloc(void *ptr, size_t size) {
  void *p = ft_realloc(ptr, size/2, size);
  if (p == NULL) {
    perror("");
    exit(EXIT_FAILURE);
  }
  return p;
}

static bool is_dir(const char *path) {
  DIR *dir = opendir(path);
  if (dir != NULL) {
    closedir(dir);
    return true;
  }
  return false;
}

/**
 * @brief コマンドライン引数をパースする
 * @param[IN] argc 引数の数
 * @param[IN/OUT] argv 引数配列
 * @return パス
 */
static struct dir_path *parse_cmd_args(int argc, char**argv) {
  int opt;
  const struct option longopts[] = {
      { "all", no_argument, NULL, 'a' },
      { "almost-all", no_argument, NULL, 'A' },
      { "color", no_argument, NULL, 'C' },
      { "classify", no_argument, NULL, 'F' },
      { "long-format", no_argument, NULL, 'l' },
      { "recursive", no_argument, NULL, 'R' },
      { "time", no_argument, NULL, 't' },
      { "reverse", no_argument, NULL, 'r' },
  };
  while ((opt = getopt_long(argc, argv, "aACFlRtr", longopts, NULL)) != -1) {
    switch (opt) {
      case 'a':
        filter = FILTER_ALL;
        break;
      case 'A':
        filter = FILTER_ALMOST;
        break;
      case 'C':
        if (isatty(STDOUT_FILENO)) {
          color = true;
        }
        break;
      case 'F':
        classify = true;
        break;
      case 'l':
        long_format = true;
        half_year_ago = time(NULL) - HALF_YEAR_SECOND;
        break;
      case 'R':
        recursive = true;
        break;
      case 't':
        time_sort = true;
        break;
      case 'r':
        reverse = true;
        break;
      default:
        return NULL;
    }
  }
  if (argc <= optind) {
    return new_dir_path("./", 0, NULL);
  } else {
    struct dir_path *head = NULL;
    struct dir_path **work = &head;
    int i;
    struct stat buf;
    bool is_multiple = false;
    if (argc - optind > 1) is_multiple = true;

    // print erros
    sort_argv(argc, argv, true);
    for (i = optind; i < argc; i++) {
      if (stat(argv[i], &buf) != 0){
        perror(argv[i]);
        continue;
      }
    }

    sort_argv(argc, argv, false);
    for (i = optind; i < argc; i++) {
      if (stat(argv[i], &buf) != 0){
        // perror(argv[i]);
        continue;
      }
      *work = new_dir_path(argv[i], is_multiple, NULL);
      work = &(*work)->next;
    }
    return head;
  }
}

/**
 * @brief モード文字列を作成する
 * @param[IN]  mode モードパラメータ
 * @param[OUT] str  文字列の出力先、11バイト以上のバッファを指定
 */
static void get_mode_string(mode_t mode, char *str) {
  str[0] = (S_ISBLK(mode))  ? 'b' :
           (S_ISCHR(mode))  ? 'c' :
           (S_ISDIR(mode))  ? 'd' :
           (S_ISREG(mode))  ? '-' :
           (S_ISFIFO(mode)) ? 'p' :
           (S_ISLNK(mode))  ? 'l' :
           (S_ISSOCK(mode)) ? 's' : '?';
  str[1] = mode & S_IRUSR ? 'r' : '-';
  str[2] = mode & S_IWUSR ? 'w' : '-';
  str[3] = mode & S_ISUID ? (mode & S_IXUSR ? 's' : 'S') : (mode & S_IXUSR ? 'x' : '-');
  str[4] = mode & S_IRGRP ? 'r' : '-';
  str[5] = mode & S_IWGRP ? 'w' : '-';
  str[6] = mode & S_ISGID ? (mode & S_IXGRP ? 's' : 'S') : (mode & S_IXGRP ? 'x' : '-');
  str[7] = mode & S_IROTH ? 'r' : '-';
  str[8] = mode & S_IWOTH ? 'w' : '-';
  str[9] = mode & S_ISVTX ? (mode & S_IXOTH ? 't' : 'T') : (mode & S_IXOTH ? 'x' : '-');
  str[10] = '\0';
}

/**
 * @brief ファイルタイプ別のインジケータを出力する
 * @param[IN] mode モードパラメータ
 */
static void print_type_indicator(mode_t mode) {
  if (S_ISREG(mode)) {
    if (mode & S_IXUGO) {
      putchar('*');
    }
  } else {
    if (S_ISDIR(mode)) {
      putchar('/');
    } else if (S_ISLNK(mode)) {
      putchar('@');
    } else if (S_ISFIFO(mode)) {
      putchar('|');
    } else if (S_ISSOCK(mode)) {
      putchar('=');
    }
  }
}

/**
 * @brief ユーザ名を表示する
 * @param[IN] uid ユーザID
 */
static void print_user(uid_t uid) {
  struct passwd *passwd = getpwuid(uid);
  if (passwd != NULL) {
    printf("%8s ", passwd->pw_name);
  } else {
    printf("%8d ", uid);
  }
}

/**
 * @brief グループ名を表示する
 * @param[IN] gid グループID
 */
static void print_group(gid_t gid) {
  struct group *group = getgrgid(gid);
  if (group != NULL) {
    printf("%8s ", group->gr_name);
  } else {
    printf("%8d ", gid);
  }
}

/**
 * @brief 時刻表示文字列を作成する
 * 半年以上前の場合は月-日 年
 * 半年以内の場合は月-日 時:分
 *
 * @param[OUT] str  格納先、12byte以上のバッファを指定
 * @param[IN]  time 文字列を作成するUNIX時間
 */
static void get_time_string(char *str, time_t time) {
  if (time - half_year_ago > 0) {
    strftime(str, 12, "%b %e %H:%M", localtime(&time));
  } else {
    strftime(str, 12, "%b %e  %Y", localtime(&time));
  }
}

/**
 * @brief ファイル名を色付き表示する
 *
 * @param[IN] name ファイル名
 * @param[IN] mode mode値
 * @param[IN] link_ok リンク先が存在しない場合にfalse
 */
static void print_name_with_color(const char *name, mode_t mode, bool link_ok) {
  if (!link_ok) {
    printf("\033[31m");
  } else if (S_ISREG(mode)) {
    if (mode & S_ISUID) {
      printf("\033[37;41m");
    } else if (mode & S_ISGID) {
      printf("\033[30;43m");
    } else if (mode & S_IXUGO) {
      printf("\033[01;32m");
    } else {
      printf("\033[0m");
    }
  } else if (S_ISDIR(mode)) {
    if ((mode & S_ISVTX) && (mode & S_IWOTH)) {
      printf("\033[30;42m");
    } else if (mode & S_IWOTH) {
      printf("\033[34;42m");
    } else if (mode & S_ISVTX) {
      printf("\033[37;44m");
    } else {
      printf("\033[01;34m");
    }
  } else if (S_ISLNK(mode)) {
    printf("\033[01;36m");
  } else if (S_ISFIFO(mode)) {
    printf("\033[33m");
  } else if (S_ISSOCK(mode)) {
    printf("\033[01;35m");
  } else if (S_ISBLK(mode)) {
    printf("\033[01;33m");
  } else if (S_ISCHR(mode)) {
    printf("\033[01;33m");
  }
  printf("%s", name);
  printf("\033[0m");
}

/**
 * @brief struct subdirのファクトリーメソッド
 * @param[IN] path パス
 * @param[IN] depth 深さ
 * @param[IN] next 次の要素へのポインタ
 * @return struct subdirへのポインタ
 */
static struct dir_path *new_dir_path(const char *path, int depth, struct dir_path *next) {
  struct dir_path *s = xmalloc(sizeof(struct dir_path));
  if (path != NULL) {
    strncpy(s->path, path, sizeof(s->path));
  }
  s->depth = depth;
  s->next = next;
  return s;
}

/**
 * @brief 可変長リストを初期化する
 * @param[OUT] list 初期化する構造体
 * @param[IN] size 初期サイズ
 */
static void init_info_list(struct info_list *list, int size) {
  list->array = xmalloc(sizeof(struct info*) * size);
  list->size = size;
  list->used = 0;
}

/**
 * @brief 可変長リスト内のメモリを開放する
 * リスト内に登録されたinfoも合わせて開放する。
 *
 * @param[IN] list 開放する構造体
 */
static void free_info_list(struct info_list *list) {
  int i;
  for (i = 0; i < list->used; i++) {
    free(list->array[i]);
  }
  free(list->array);
}

/**
 * @brief 可変長リストへ情報を格納する
 * 格納場所がない場合は拡張を行う
 *
 * @param[IN/OUT] list 格納先構造体
 * @param[IN] info 格納するデータ
 */
static void add_info(struct info_list *list, struct info *info) {
  if (list->size == list->used) {
    list->size = list->size * 2;
    list->array = xrealloc(list->array, sizeof(struct info*) * list->size);
  }
  list->array[list->used] = info;
  list->used++;
}

/**
 * @brief エントリ情報構造体のファクトリメソッド
 * メモリ確保から、指定パスの各情報格納までを行う
 *
 * @param[IN] path エントリのパス
 * @param[IN] name エントリの名前
 * @return エントリ情報構造体
 */
static struct info *new_info(const char *path, const char *name) {
  struct info *info = xmalloc(sizeof(struct info));
  strncpy(info->name, name, NAME_MAX + 1);
  if (lstat(path, &info->stat) != 0) {
    perror(path);
    free(info);
    return NULL;
  }
  info->link_ok = false;
  info->link[0] = 0;
  info->link_mode = 0;
  if (S_ISLNK(info->stat.st_mode)) {
    struct stat link_stat;
    int link_len = readlink(path, info->link, PATH_MAX);
    if (link_len > 0) {
      info->link[link_len] = 0;
    }
    if (stat(path, &link_stat) == 0) {
      info->link_ok = true;
      info->link_mode = link_stat.st_mode;
    }
  } else {
    info->link_ok = true;
  }
  return info;
}

/**
 * @brief ソート用ファイル名比較
 * @param[IN] a
 * @param[IN] b
 * @return a>bなら正、a==bなら0、a<bなら負
 */
static int compare_name(const void *a, const void *b) {
  struct info *ai = *(struct info**)a;
  struct info *bi = *(struct info**)b;
  return strcmp(ai->name, bi->name);
}

static int timespec_cmp(struct timespec a, struct timespec b) {
  return (a.tv_sec < b.tv_sec ? -1
	  : a.tv_sec > b.tv_sec ? 1
	  : a.tv_nsec - b.tv_nsec);
}

static void reverse_list(struct info_list *list, size_t size) {
  for (size_t i = 0; i < size / 2; ++i) {
    struct info *tmp = list->array[i];
    list->array[i] = list->array[size-i-1];
    list->array[size-i-1] = tmp;
  }
}

static void reverse_argv(char **argv, size_t size) {
  size_t i = 0;

  while (i < size && is_dir(argv[i]) == false) {
    ++i;
    --size;
  }
  // printf("i=%zu, size=%zu\n", i, size);
  size_t start = i;


  for (i = 0; i < start/2; ++i) {
    char *tmp = argv[i];
    argv[i] = argv[start-i-1];
    argv[start-i-1] = tmp;
  }

  i = 0;
  // while (argv[i]) {
  //   printf("argv[%zu]=%s\n", i, argv[i]);
  //   ++i;
  // }
  // i = 0;

  for (i = start; i < start + size / 2; ++i) {
    char *tmp = argv[i];
    argv[i] = argv[start+size-(i-start)-1];
    argv[start+size-(i-start)-1] = tmp;
  }
}

/**
 * @brief ソート用修正時間比較
 * @param[IN] a
 * @param[IN] b
 * @return a>bなら正、a==bなら0、a<bなら負
 */
static int compare_time(const void *a, const void *b) {
  struct info *ai = *(struct info**)a;
  struct info *bi = *(struct info**)b;
  int diff = timespec_cmp(bi->stat.st_mtimespec, ai->stat.st_mtimespec);
  return diff ? diff : strcmp (ai->name, bi->name);
}

/**
 * @brief ファイル、ディレクトリ順のソート用ファイル名比較
 * @param[IN] a
 * @param[IN] b
 * @return a>bなら正、a==bなら0、a<bなら負
 */
static int compare_name_file_dir(const void *a, const void *b) {
  char *path_a = *(char**)a;
  char *path_b = *(char**)b;
  if (is_dir(path_a) && !is_dir(path_b)) {
    return 1;
  }
  if (!is_dir(path_a) && is_dir(path_b)) {
    return -1;
  }
  return strcmp(path_a, path_b);
}

/**
 * @brief ソート用修正時間比較
 * @param[IN] a
 * @param[IN] b
 * @return a>bなら正、a==bなら0、a<bなら負
 */
static int compare_time_file(const void *a, const void *b) {
  char *a_path = *(char**)a;
  char *b_path = *(char**)b;
  struct stat a_buf;
  struct stat b_buf;
  if (is_dir(a_path) && !is_dir(b_path)) {
    return 1;
  }
  if (!is_dir(a_path) && is_dir(b_path)) {
    return -1;
  }
  stat(a_path, &a_buf);
  stat(b_path, &b_buf);
  int diff = timespec_cmp(b_buf.st_mtimespec, a_buf.st_mtimespec);
  return diff ? diff : strcmp (a_path, b_path);
  }

/**
 * @brief リスト内のソートを行う
 * @param[IN/OUT] ソート対象のリスト
 */
static void sort_list(struct info_list *list) {
  if (time_sort) {
    qsort(list->array, list->used, sizeof(struct info *), compare_time);
  } else {
    qsort(list->array, list->used, sizeof(struct info*), compare_name);
  }
  if (reverse) {
    reverse_list(list, list->used);
  }
}

/**
 * @brief argvのソートを行う
 * @param[IN/OUT] ソート対象のargv
 */
static void sort_argv(int argc, char **argv, bool error_print) {
  if (time_sort && !error_print) {
    qsort(&argv[optind], argc - optind, sizeof(char *), compare_time_file);
  } else {
    qsort(&argv[optind], argc - optind, sizeof(char *), compare_name_file_dir);
  }
  if (reverse && !error_print) {
    reverse_argv(&argv[optind], argc - optind);
  }
}

/**
 * @brief エントリ情報に基づいて情報を表示する
 * @param[IN] info 表示する情報
 */
static void print_info(struct info *info) {
  if (long_format) {
    char buf[13];
    get_mode_string(info->stat.st_mode, buf);
    printf("%s ", buf);
    printf("%3d ", (int)info->stat.st_nlink);
    print_user(info->stat.st_uid);
    print_group(info->stat.st_gid);
    if (S_ISCHR(info->stat.st_mode) || S_ISBLK(info->stat.st_mode)) {
      printf("%4d,%4d ", MAJOR(info->stat.st_rdev),
             MINOR(info->stat.st_rdev));
    } else {
      printf("%9lld ", info->stat.st_size);
    }
    get_time_string(buf, info->stat.st_mtimespec.tv_sec);
    printf("%s ", buf);
  }
  if (color) {
    print_name_with_color(info->name, info->stat.st_mode, info->link_ok);
  } else {
    printf("%s", info->name);
  }
  if (classify) {
    print_type_indicator(info->stat.st_mode);
  }
  if (long_format) {
    if (info->link[0] != 0) {
      printf(" -> ");
      if (color) {
        print_name_with_color(info->link, info->link_mode, info->link_ok);
      } else {
        printf("%s", info->link);
      }
    }
  }
  putchar('\n');
}

/**
 * @brief パス名からファイル名を取り出す
 * @param[IN] path パス名
 * @return path名内のファイル名を指すポインタ
 */
static const char *find_filename(const char *path) {
  int i;
  size_t path_len = strlen(path);
  for (i = path_len;i >= 0; i--) {
    if (path[i] == '/') {
      return &path[i+1];
    }
  }
  return path;
}

/**
 * @brief 指定パスのディレクトリエントリをリストする
 * @param[IN] base パス
 */
static void list_dir(struct dir_path *base) {
  const char *base_path = base->path;
  int i;
  DIR *dir;
  struct dirent *dent;
  char path[PATH_MAX + 1];
  size_t path_len;
  struct info_list list;
  struct dir_path *subque = base;
  dir = opendir(base_path);
  if (dir == NULL) {
    if (errno == ENOTDIR) {
      const char *name = find_filename(base_path);
      struct info *info = new_info(base_path, name);
      if (info != NULL) {
        print_info(info);
        free(info);
      }
    } else {
      perror(base_path);
    }
    return;
  }
  path_len = strlen(base_path);
  if (path_len >= PATH_MAX - 1) {
    fprintf(stderr, "too long path\n");
    return;
  }
  strncpy(path, base_path, PATH_MAX);
  if (path[path_len - 1] != '/') {
    path[path_len] = '/';
    path_len++;
    path[path_len] = '\0';
  }
  init_info_list(&list, 100);
  while ((dent = readdir(dir)) != NULL) {
    struct info *info;
    const char *name = dent->d_name;
    if (filter != FILTER_ALL
        && name[0] == '.'
        && (filter == FILTER_DEFAULT
            || name[1 + (name[1] == '.')] == '\0')) {
      continue;
    }
    strncpy(&path[path_len], dent->d_name, PATH_MAX - path_len);
    info = new_info(path, name);
    if (info == NULL) {
      continue;
    }
    add_info(&list, info);
    block_size += info->stat.st_blocks;
    // block_size += info->stat.st_blksize;
  }
  closedir(dir);
  sort_list(&list);
  bool total_printed = true;
  for (i = 0; i < list.used; i++) {
    struct info *info = list.array[i];
    if (recursive && S_ISDIR(info->stat.st_mode)) {
      const char *name = info->name;
      if (!(name[0] == '.'
          && name[1 + (name[1] == '.')] == '\0')) {
        strncpy(&path[path_len], name, PATH_MAX - path_len);
        subque->next = new_dir_path(path, base->depth + 1, subque->next);
        subque = subque->next;
      }
    }
    if (long_format && total_printed) {
      printf("total %u\n", block_size);
      block_size = 0;
      total_printed = false;
    }
    print_info(info);
  }
  free_info_list(&list);
}

int main(int argc, char**argv) {
  struct dir_path *head = parse_cmd_args(argc, argv);
  if (head == NULL) {
    return EXIT_FAILURE;
  }
  while (head != NULL) {
    if (head->depth != 0) {
      if (is_dir(head->path)) {
        if (!first) printf("\n");
        printf("%s:\n", head->path);
      }
      first = false;
    }
    list_dir(head);
    struct dir_path *tmp = head;
    head = head->next;
    free(tmp);
  }
  return EXIT_SUCCESS;
}
