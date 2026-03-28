#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>


struct node {
    char *word;
    int count;
    double freq;         
    struct node *next;
};

struct file_storage {
    char *path;
    int total_words;     
    struct node *words;  /* head of sorted word linked list */
    struct file_storage *next;
};

/* 
 * Walks the list to find the right alphabetical position.
 * If the word already exists, increments count.
 * If not, creates a new node and splices it in sorted order.
 */
void insert(struct node **head, const char *word) {
    struct node *prev = NULL;
    struct node *cur  = *head;

    while (cur) {
        int cmp = strcmp(cur->word, word);
        if (cmp == 0) { cur->count++; return; }
        if (cmp > 0) break;
        prev = cur;
        cur  = cur->next;
    }

    struct node *n = malloc(sizeof *n);
    n->word  = strdup(word);
    n->count = 1;
    n->freq  = 0.0;
    n->next  = cur;

    if (prev == NULL) *head = n;
    else prev->next = n;
}

int is_txt(char *filename) {
    int len = strlen(filename);
    if (len < 4) return 0;
    return strcmp(filename + len - 4, ".txt") == 0;
}

/* 
 * Opens the file, reads it in chunks, builds words character by character.
 * Word characters: letters, digits, dash. Everything else ends the word.
 * All characters lowercased before storing.
 * After reading, divides each count by total_words to get frequency.
 */
#define BUFSIZE 4096

void read_words(struct file_storage *fs) {
    int fd = open(fs->path, O_RDONLY);
    if (fd < 0) {
        perror(fs->path);
        return;
    }

    char buf[BUFSIZE];
    ssize_t bytes;

    char *word_buf = NULL;
    int word_len   = 0;
    int word_cap   = 0;

    while ((bytes = read(fd, buf, BUFSIZE)) > 0) {
        for (int i = 0; i < bytes; i++) {
            unsigned char ch = buf[i];

            int is_word_char = isalpha(ch) || isdigit(ch) || ch == '-';

            if (is_word_char) {
                
                if (word_len + 2 > word_cap) {
                    word_cap = word_cap == 0 ? 64 : word_cap * 2;
                    word_buf = realloc(word_buf, word_cap);
                }
                word_buf[word_len++] = (char)tolower(ch);
            } else {
                
                if (word_len > 0) {
                    word_buf[word_len] = '\0';
                    insert(&fs->words, word_buf);
                    fs->total_words++;
                    word_len = 0;
                }
            }
        }
    }

    /* catch last word if file doesn't end with whitespace */
    if (word_len > 0) {
        word_buf[word_len] = '\0';
        insert(&fs->words, word_buf);
        fs->total_words++;
    }

    close(fd);
    free(word_buf);

    /* compute frequencies */
    if (fs->total_words > 0) {
        for (struct node *n = fs->words; n; n = n->next) {
            n->freq = (double)n->count / (double)fs->total_words;
        }
    }
}

void recursive_traversal(char *path, struct file_storage **file_list) {
    DIR *dir = opendir(path);
    if (!dir) { perror(path); return; }

    int path_len = strlen(path);
    struct dirent *de;

    while ((de = readdir(dir))) {
        if (de->d_name[0] == '.') continue;

        int name_len    = strlen(de->d_name);
        char *file_path = malloc(path_len + name_len + 2);
        memcpy(file_path, path, path_len);
        file_path[path_len] = '/';
        memcpy(file_path + path_len + 1, de->d_name, name_len + 1);

        struct stat sb;
        if (stat(file_path, &sb) < 0) {
            perror(file_path);
            free(file_path);
            continue;
        }

        if (S_ISDIR(sb.st_mode)) {
            recursive_traversal(file_path, file_list);
        } else {
            if (S_ISREG(sb.st_mode) && is_txt(de->d_name)) {
                struct file_storage *new_fs = malloc(sizeof *new_fs);
                new_fs->path        = strdup(file_path);
                new_fs->total_words = 0;
                new_fs->words       = NULL;
                new_fs->next        = *file_list;
                *file_list          = new_fs;
            }
        }
        free(file_path);
    }
    closedir(dir);
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file-or-dir> ...\n", argv[0]);
        return 1;
    }

    struct file_storage *file_list = NULL;

    for (int i = 1; i < argc; i++) {
        struct stat sb;
        if (stat(argv[i], &sb) < 0) { perror(argv[i]); continue; }

        if (S_ISDIR(sb.st_mode)) {
            recursive_traversal(argv[i], &file_list);
        } else if (S_ISREG(sb.st_mode)) {
            struct file_storage *new_fs = malloc(sizeof *new_fs);
            new_fs->path        = strdup(argv[i]);
            new_fs->total_words = 0;
            new_fs->words       = NULL;
            new_fs->next        = file_list;
            file_list           = new_fs;
        }
    }

    /* read words for every file we collected */
    for (struct file_storage *f = file_list; f; f = f->next) {
        read_words(f);
    }

    return 0;
}