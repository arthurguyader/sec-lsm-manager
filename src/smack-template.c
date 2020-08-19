/*
 * Copyright (C) 2020 "IoT.bzh"
 * Author Arthur Guyader <arthur.guyader@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "smack-template.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "smack-label.h"
#include "utils.h"

#define MAX_SMACK_LABEL_SIZE SMACK_LABEL_LEN
#define MAX_ACCESS_SIZE 6
#define MAX_SMACK_RULE_SIZE MAX_SMACK_LABEL_SIZE * 2 + MAX_ACCESS_SIZE + 2

#define SMACK_COMMENT_CHAR '#'

#if !defined(DEFAULT_TEMPLATE_DIR)
#define DEFAULT_TEMPLATE_DIR "/usr/share/security-manager/"
#endif

#if !defined(DEFAULT_TEMPLATE_FILE)
#define DEFAULT_TEMPLATE_FILE "app-template.smack"
#endif

#if !defined(DEFAULT_SMACK_TEMPLATE_FILE)
#define DEFAULT_SMACK_TEMPLATE_FILE DEFAULT_TEMPLATE_DIR DEFAULT_TEMPLATE_FILE
#endif

#if !defined(DEFAULT_SMACK_RULES_DIR)
#define DEFAULT_SMACK_RULES_DIR "/etc/smack/accesses.d/"
#endif

const char default_smack_template_file[] = DEFAULT_SMACK_TEMPLATE_FILE;
const char default_smack_rules_dir[] = DEFAULT_SMACK_RULES_DIR;

const char prefix_app_rules[] = "app-";

#define REPLACE_APP "~APP~"

typedef struct smack_handle {
    const char *id;
    char *app_label;
    struct smack_accesses *smack_accesses;
} smack_handle_t;

/***********************/
/*** PRIVATE METHODS ***/
/***********************/

/**
 * @brief Free smack handle
 * The pointer is not free
 * @param[in] smack_handle smack_handle handler
 */
static void free_smack_handle(smack_handle_t *smack_handle) {
    if (smack_handle) {
        if (smack_handle->smack_accesses) {
            smack_accesses_free(smack_handle->smack_accesses);
            smack_handle->smack_accesses = NULL;
        }
        free((void *)smack_handle->id);
        smack_handle->id = NULL;
        free((void *)smack_handle->app_label);
        smack_handle->app_label = NULL;
    }
}

/**
 * @brief Init smack handle
 *
 * @param[in] smack_handle smack_handle handler
 * @param[in] id id of application
 * @return 0 in case of success or a negative -errno value
 */
static int init_smack_handle(smack_handle_t *smack_handle, const char *id) {
    if (!id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    int rc = 0;
    smack_handle->id = strdup(id);

    if (!smack_handle->id) {
        rc = -errno;
        ERROR("strdup id %m");
        return rc;
    }

    rc = generate_label(&(smack_handle->app_label), id, prefix_app, NULL);
    if (rc < 0) {
        ERROR("generate_label");
        free_smack_handle(smack_handle);
        return rc;
    }

    rc = smack_accesses_new(&(smack_handle->smack_accesses));
    if (rc < 0) {
        ERROR("smack_accesses_new");
        free_smack_handle(smack_handle);
        return rc;
    }

    return 0;
}

/**
 * @brief Count the number of space in a line
 *
 * @param[in] line The line to parse
 * @return number of space in case of success or a negative -errno value
 */
static int count_space(const char *line) {
    if (!line) {
        ERROR("line undefined");
        return -EINVAL;
    }
    size_t len = strlen(line);
    int count = 0;
    for (size_t i = 0; i < len; i++) {
        if (line[i] == ' ')
            count++;
    }
    return count;
}

/**
 * @brief Parse a line and add the rule in smack handle
 *
 * @param[in] line The smack rule line to parse
 * @param[in] smack_handle smack_handle handler
 * @return 0 in case of success or a negative -errno value
 */
static int parse_line(char *line, const smack_handle_t *smack_handle) {
    if (!line) {
        ERROR("line undefined");
        return -EINVAL;
    } else if (!smack_handle) {
        ERROR("smack_handle undefined");
        return -EINVAL;
    } else if (!smack_handle->app_label) {
        ERROR("app_label undefined");
        return -EINVAL;
    } else if (line[0] == SMACK_COMMENT_CHAR) {  // comment
        return 0;
    } else if (line[0] == '\n') {  // new line
        return 0;
    }

    char subject[MAX_SMACK_LABEL_SIZE];
    char object[MAX_SMACK_LABEL_SIZE];
    char access[MAX_SMACK_LABEL_SIZE];

    // replace id
    char *pos_str = NULL;
    char after[MAX_SMACK_RULE_SIZE] = {0};

    line[strcspn(line, "\n")] = 0;
    while ((pos_str = strstr(line, REPLACE_APP))) {
        strcpy(after, pos_str + strlen(REPLACE_APP));  // save overwrite data
        strcpy(pos_str, smack_handle->app_label);
        strcpy(pos_str + strlen(smack_handle->app_label), after);
    }

    // check valid rule
    int c = count_space(line);
    if (c != 2) {
        printf("Invalid rules");
        return -EINVAL;
    }

    // subject
    char *ptr = strtok(line, " ");
    strncpy(subject, ptr, MAX_SMACK_LABEL_SIZE);

    // object
    ptr = strtok(NULL, " ");
    strncpy(object, ptr, MAX_SMACK_LABEL_SIZE);

    // access
    ptr = strtok(NULL, " ");
    strncpy(access, ptr, MAX_ACCESS_SIZE);

    int rc = smack_accesses_add(smack_handle->smack_accesses, subject, object, access);
    if (rc < 0) {
        ERROR("smack_accesses_add");
        return rc;
    }

    return 0;
}

/**
 * @brief Parse the smack template file and add rules in smack_handle
 *
 * @param[in] smack_template_file The template file
 * @param[in] smack_handle smack_handle handler
 * @return 0 in case of success or a negative -errno value
 */
static int parse_template_file(const char *smack_template_file, const smack_handle_t *smack_handle) {
    if (!smack_template_file) {
        ERROR("smack_template_file undefined");
        return -EINVAL;
    } else if (!smack_handle) {
        ERROR("smack_handle undefined");
        return -EINVAL;
    }

    int rc = 0;
    char line[MAX_SMACK_LABEL_SIZE];
    FILE *f = fopen(smack_template_file, "r");

    if (!f) {
        rc = -errno;
        ERROR("fopen : %m");
        return rc;
    }

    while (rc >= 0 && fgets(line, MAX_SMACK_LABEL_SIZE, f)) {
        rc = parse_line(line, smack_handle);
    }

    if (rc < 0) {
        ERROR("parse_line : %s", line);
    }

    if (fclose(f)) {
        ERROR("fclose %s : %m", smack_template_file);
    }

    return rc;
}

/**
 * @brief Get smack rules file path specification
 *
 * @param[in] smack_rules_dir some value or NULL for getting default
 * @param[in] id id of the application
 * @return smack rules file path if success, NULL if error
 */
static char *get_smack_rules_file_path(const char *smack_rules_dir, const char *id) {
    if (!smack_rules_dir) {
        ERROR("smack_rules_dir undefined");
        return NULL;
    } else if (!id) {
        ERROR("id undefined");
        return NULL;
    }

    size_t len = strlen(smack_rules_dir) + strlen(prefix_app_rules) + strlen(id);
    char *file = (char *)malloc(len + 1);
    if (!file) {
        ERROR("malloc");
        return NULL;
    }
    memset(file, 0, len + 1);

    strcpy(file, smack_rules_dir);
    strcat(file, prefix_app_rules);
    strcat(file, id);

    return file;
}

/**
 * @brief save the rules in smack rules dir and if smack enable load them directly
 *
 * @param[in] smack_rules_dir The smack rules directory
 * @param[in] smack_handle smack handle handler with some accesses added
 * @return 0 in case of success or a negative -errno value
 */
static int apply_save_accesses_file(const char *smack_rules_dir, smack_handle_t *smack_handle) {
    if (!smack_rules_dir) {
        ERROR("smack_rules_dir undefined");
        return -EINVAL;
    } else if (!smack_handle) {
        ERROR("smack_handle undefined");
        return -EINVAL;
    }

    int rc = 0;

    if (smack_enabled()) {
        rc = smack_accesses_apply(smack_handle->smack_accesses);
        if (rc < 0) {
            ERROR("smack_accesses_apply");
            return rc;
        }
    }

    char *file = get_smack_rules_file_path(smack_rules_dir, smack_handle->id);

    if (!file) {
        ERROR("get_smack_rules_file_path");
        return -EINVAL;
    }

    int fd = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644);

    if (fd < 0) {
        rc = -errno;
        ERROR("open : %m");
        goto end;
    }

    rc = smack_accesses_save(smack_handle->smack_accesses, fd);
    if (rc < 0) {
        ERROR("smack_accesses_save");
        goto end2;
    }

end2:
    if (close(fd)) {
        ERROR("close : %m");
    }
end:
    free(file);
    file = NULL;
    return rc;
}

/**
 * @brief Remove file and loaded rules
 *
 * @param[in] file The path of the file to remove
 * @return 0 in case of success or a negative -errno value
 */
static int remove_load_rules(const char *file) {
    if (!file) {
        ERROR("file undefined");
        return -EINVAL;
    }
    int rc = 0;
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
        rc = -errno;
        ERROR("open file : %s", file);
        return rc;
    }

    struct smack_accesses *smack_access = NULL;
    rc = smack_accesses_new(&smack_access);
    if (rc < 0) {
        ERROR("smack_accesses_new");
        goto end;
    }

    rc = smack_accesses_add_from_file(smack_access, fd);
    if (rc < 0) {
        ERROR("smack_accesses_add_from_file");
        goto end2;
    }

    rc = smack_accesses_clear(smack_access);
    if (rc < 0) {
        ERROR("smack_accesses_clear");
        goto end2;
    }

end2:
    smack_accesses_free(smack_access);
end:
    if (close(fd)) {
        ERROR("close");
    }
    return rc;
}

/**********************/
/*** PUBLIC METHODS ***/
/**********************/

/* see smack-template.h */
const char *get_smack_template_file(const char *value) {
    return value ?: secure_getenv("SMACK_TEMPLATE_FILE") ?: default_smack_template_file;
}

/* see smack-template.h */
const char *get_smack_rules_dir(const char *value) {
    return value ?: secure_getenv("SMACK_RULES_DIR") ?: default_smack_rules_dir;
}

/* see smack-template.h */
int create_smack_rules(const secure_app_t *secure_app, const char *smack_template_file, const char *smack_rules_dir) {
    if (!secure_app) {
        ERROR("secure_app undefined");
        return -EINVAL;
    } else if (!secure_app->id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    smack_handle_t smack_handle = {0};

    smack_template_file = get_smack_template_file(smack_template_file);
    smack_rules_dir = get_smack_rules_dir(smack_rules_dir);

    int rc = init_smack_handle(&smack_handle, secure_app->id);
    if (rc < 0) {
        ERROR("init_smack_handle");
        return rc;
    }

    rc = parse_template_file(smack_template_file, &smack_handle);
    if (rc < 0) {
        ERROR("parse_template_file")
        free_smack_handle(&smack_handle);
        return rc;
    }

    rc = apply_save_accesses_file(smack_rules_dir, &smack_handle);
    if (rc < 0) {
        ERROR("apply_save_accesses_file");
        free_smack_handle(&smack_handle);
        return rc;
    }

    free_smack_handle(&smack_handle);

    return 0;
}

/* see smack-template.h */
int remove_smack_rules(const secure_app_t *secure_app, const char *smack_rules_dir) {
    if (!secure_app) {
        ERROR("secure_app undefined");
        return -EINVAL;
    } else if (!secure_app->id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    int rc = 0;
    smack_rules_dir = get_smack_rules_dir(smack_rules_dir);
    char *file = get_smack_rules_file_path(smack_rules_dir, secure_app->id);

    if (!file) {
        ERROR("get_smack_rules_file_path");
        rc = -EINVAL;
        goto end;
    }

    if (smack_enabled()) {
        rc = remove_load_rules(file);
        if (rc < 0) {
            ERROR("remove_load_rules");
            goto end2;
        }
    }

    rc = remove_file(file);
    if (rc < 0) {
        ERROR("remove");
        goto end2;
    }

end2:
    free(file);
end:
    file = NULL;
    return rc;
}