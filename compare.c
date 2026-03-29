#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

struct node {
    char *word;
    int count;
    double freq;
    struct node *next;
};

struct file_storage {
    char *path;
    int total_words;
    struct node *words; /* head of sorted word linked list */
    struct file_storage *next;
};

struct comparison {
    struct file_storage *f1;
    struct file_storage *f2;
    int combined_words;
    double jsd;
};

/*
 * Walks the list to find the right alphabetical position.
 * If the word already exists, increments count.
 * If not, creates a new node and splices it in sorted order.
 */
void insert(struct node **head, const char *word) {
    struct node *prev = NULL;
    struct node *cur = *head;

    while (cur) {
        int cmp = strcmp(cur->word, word);
        if (cmp == 0) { cur->count++; return; }
        if (cmp > 0) break;
        prev = cur;
        cur = cur->next;
    }

    struct node *n = malloc(sizeof *n);
    n->word = strdup(word);
    n->count = 1;
    n->freq = 0.0;
    n->next = cur;

    if (prev == NULL) *head = n;
    else prev->next = n;
}

/* returns 1 if filename ends with .txt, 0 otherwise */
int is_txt(char *filename) {
    int len = strlen(filename);
    if (len < 4) return 0;
    return strcmp(filename + len - 4, ".txt") == 0;
}

#define BUFSIZE 4096

/*
 * Opens the file, reads it in chunks, builds words character by character.
 * Word characters: letters, digits, dash. Only whitespace ends a word,
 * everything else like punctuation is skipped.
 * All characters lowercased before storing.
 * After reading, divides each count by total_words to get frequency.
 */
void read_words(struct file_storage *fs) {
    int fd = open(fs->path, O_RDONLY);
    if (fd < 0) {
        perror(fs->path);
        return;
    }

    char buf[BUFSIZE];
    ssize_t bytes;

    char *word_buf = NULL;
    int word_len = 0;
    int word_cap = 0;

    while ((bytes = read(fd, buf, BUFSIZE)) > 0) {
        for (int i = 0; i < bytes; i++) {
            unsigned char ch = buf[i];

            int is_word_char = isalpha(ch) || isdigit(ch) || ch == '-';

            if (is_word_char) {
                /* grow buffer if needed */
                if (word_len + 2 > word_cap) {
                    word_cap = word_cap == 0 ? 64 : word_cap * 2;
                    word_buf = realloc(word_buf, word_cap);
                }
                word_buf[word_len++] = (char)tolower(ch);
            }

            else if (isspace(ch)) {
                /* whitespace ends the current word */
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

/* recursively traverses directories and adds .txt files to file_list */
void recursive_traversal(char *path, struct file_storage **file_list) {
    DIR *dir = opendir(path);
    if (!dir) { perror(path); return; }

    int path_len = strlen(path);
    struct dirent *de;

    while ((de = readdir(dir))) {
        /* skip hidden files and directories */
        if (de->d_name[0] == '.') continue;

        int name_len = strlen(de->d_name);
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
        }

        else {
            if (S_ISREG(sb.st_mode) && is_txt(de->d_name)) {
                struct file_storage *new_fs = malloc(sizeof *new_fs);
                new_fs->path = strdup(file_path);
                new_fs->total_words = 0;
                new_fs->words = NULL;
                new_fs->next = *file_list;
                *file_list = new_fs;
            }
        }
        free(file_path);
    }
    closedir(dir);
}

/* computes p * log2(p/m), returns 0 if either value is 0 */
static double kld_term(double p, double m) {
    if (p == 0.0 || m == 0.0)
        return 0.0;
    return p * log2(p / m);
}

/* comparator for qsort: sorts by combined word count descending */
int cmp_comparisons(const void *a, const void *b) {
    const struct comparison *ca = (const struct comparison *)a;
    const struct comparison *cb = (const struct comparison *)b;
    return cb->combined_words - ca->combined_words;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file-or-dir> ...\n", argv[0]);
        return 1;
    }

    struct file_storage *file_list = NULL;

    /* collect all files from arguments */
    for (int i = 1; i < argc; i++) {
        struct stat sb;
        if (stat(argv[i], &sb) < 0) { perror(argv[i]); continue; }

        if (S_ISDIR(sb.st_mode)) {
            recursive_traversal(argv[i], &file_list);
        }

        else if (S_ISREG(sb.st_mode)) {
            struct file_storage *new_fs = malloc(sizeof *new_fs);
            new_fs->path = strdup(argv[i]);
            new_fs->total_words = 0;
            new_fs->words = NULL;
            new_fs->next = file_list;
            file_list = new_fs;
        }
    }

    /* read words for every file we collected */
    for (struct file_storage *f = file_list; f; f = f->next) {
        read_words(f);
    }

    /* count files */
    int n = 0;
    for (struct file_storage *f = file_list; f; f = f->next) n++;

    if (n < 2) {
        fprintf(stderr, "Error: need at least 2 files\n");
        return 1;
    }

    /* put files into an array so we can index them */
    struct file_storage **files = malloc(n * sizeof *files);
    int idx = 0;
    for (struct file_storage *f = file_list; f; f = f->next)
        files[idx++] = f;

    /* build comparison array */
    int num_pairs = n * (n - 1) / 2;
    struct comparison *comps = malloc(num_pairs * sizeof *comps);
    int k = 0;

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double kld_a = 0.0;
            double kld_b = 0.0;

            struct node *word_a = files[i]->words;
            struct node *word_b = files[j]->words;

            /* merge walk both sorted word lists simultaneously */
            while (word_a != NULL || word_b != NULL) {
                double freq1, freq2;
                int cmp;

                if (word_a == NULL) {
                    cmp = 1;
                }

                else if (word_b == NULL) {
                    cmp = -1;
                }

                else {
                    cmp = strcmp(word_a->word, word_b->word);
                }

                if (cmp == 0) {
                    /* word in both files */
                    freq1 = word_a->freq;
                    freq2 = word_b->freq;
                    word_a = word_a->next;
                    word_b = word_b->next;
                }

                else if (cmp < 0) {
                    /* word only in file i */
                    freq1 = word_a->freq;
                    freq2 = 0.0;
                    word_a = word_a->next;
                }

                else {
                    /* word only in file j */
                    freq1 = 0.0;
                    freq2 = word_b->freq;
                    word_b = word_b->next;
                }

                double m = (freq1 + freq2) / 2.0;
                kld_a += kld_term(freq1, m);
                kld_b += kld_term(freq2, m);
            }

            comps[k].f1 = files[i];
            comps[k].f2 = files[j];
            comps[k].combined_words = files[i]->total_words + files[j]->total_words;
            comps[k].jsd = sqrt(0.5 * kld_a + 0.5 * kld_b);
            k++;
        }
    }

    /* sort by combined word count descending */
    qsort(comps, num_pairs, sizeof *comps, cmp_comparisons);

    /* print results */
    for (int i = 0; i < num_pairs; i++)
        printf("%.5f %s %s\n", comps[i].jsd, comps[i].f1->path, comps[i].f2->path);

    free(files);
    free(comps);
    return 0;
}