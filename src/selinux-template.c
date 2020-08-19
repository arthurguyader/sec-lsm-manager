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

#include "selinux-template.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "selinux-compile.h"
#include "selinux-label.h"
#include "utils.h"

#define MAX_LINE_SIZE_MODULE 2048

#define REPLACE_APP "~APP~"
#define REPLACE_ID "~ID~"

#if !defined(DEFAULT_TEMPLATE_DIR)
#define DEFAULT_TEMPLATE_DIR "/usr/share/security-manager/"
#endif

#define SIZE_EXTENSION 3
#define TE_EXTENSION ".te"
#define FC_EXTENSION ".fc"
#define IF_EXTENSION ".if"
#define PP_EXTENSION ".pp"

#if !defined(DEFAULT_TE_TEMPLATE_FILE)
#define DEFAULT_TE_TEMPLATE_FILE "app-template.te"
#endif

#if !defined(DEFAULT_IF_TEMPLATE_FILE)
#define DEFAULT_IF_TEMPLATE_FILE "app-template.if"
#endif

#if !defined(DEFAULT_SELINUX_TE_TEMPLATE_FILE)
#define DEFAULT_SELINUX_TE_TEMPLATE_FILE DEFAULT_TEMPLATE_DIR DEFAULT_TE_TEMPLATE_FILE
#endif

#if !defined(DEFAULT_SELINUX_IF_TEMPLATE_FILE)
#define DEFAULT_SELINUX_IF_TEMPLATE_FILE DEFAULT_TEMPLATE_DIR DEFAULT_IF_TEMPLATE_FILE
#endif

#if !defined(DEFAULT_SELINUX_RULES_DIR)
#define DEFAULT_SELINUX_RULES_DIR "/usr/share/security-manager/selinux-policy/"
#endif

const char default_selinux_rules_dir[] = DEFAULT_SELINUX_RULES_DIR;
const char default_selinux_te_template_file[] = DEFAULT_SELINUX_TE_TEMPLATE_FILE;
const char default_selinux_if_template_file[] = DEFAULT_SELINUX_IF_TEMPLATE_FILE;

typedef struct selinux_module {
    char *id;                              // my-id
    char *selinux_id;                      // my_id
    char *selinux_te_file;                 ///////////////////
    char *selinux_if_file;                 //   PATH MODULE //
    char *selinux_fc_file;                 //      FILE     //
    char *selinux_pp_file;                 ///////////////////
    const char *selinux_rules_dir;         // Store te, if, fc, pp files
    const char *selinux_te_template_file;  // te base template
    const char *selinux_if_template_file;  // if base template
} selinux_module_t;

/***********************/
/*** PRIVATE METHODS ***/
/***********************/

/**
 * @brief Free selinux_pp_file, selinux_if_file, selinux_fc_file, selinux_te_file
 *
 * @param[in] selinux_module selinux_module handler
 */
static void free_path_module_files(selinux_module_t *selinux_module) {
    if (selinux_module) {
        free(selinux_module->selinux_pp_file);
        selinux_module->selinux_pp_file = NULL;

        free(selinux_module->selinux_if_file);
        selinux_module->selinux_if_file = NULL;

        free(selinux_module->selinux_fc_file);
        selinux_module->selinux_fc_file = NULL;

        free(selinux_module->selinux_te_file);
        selinux_module->selinux_te_file = NULL;
    }
}

/**
 * @brief Free all fields of selinux modules or set to NULL
 * The pointer is not free
 * @param[in] selinux_module selinux_module handler
 */
static void free_selinux_module(selinux_module_t *selinux_module) {
    if (selinux_module) {
        free(selinux_module->id);
        selinux_module->id = NULL;
        free(selinux_module->selinux_id);
        selinux_module->selinux_id = NULL;
        free_path_module_files(selinux_module);
        selinux_module->selinux_rules_dir = NULL;
        selinux_module->selinux_te_template_file = NULL;
        selinux_module->selinux_if_template_file = NULL;
    }
}

/**
 * @brief Allocate selinux_pp_file, selinux_if_file, selinux_fc_file, selinux_te_file
 *
 * @param[in] selinux_module selinux_module handler
 * @return 0 in case of success or a negative -errno value
 */
static int generate_path_module_files(selinux_module_t *selinux_module) {
    if (!selinux_module->selinux_rules_dir) {
        ERROR("selinux_rules_dir undefined");
        return -EINVAL;
    } else if (!selinux_module->id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    size_t len = strlen(selinux_module->selinux_rules_dir) + strlen(selinux_module->id) + SIZE_EXTENSION + 1;

    selinux_module->selinux_te_file = malloc(len);
    if (!selinux_module->selinux_te_file) {
        ERROR("malloc selinux_te_file");
        free_path_module_files(selinux_module);
        return -ENOMEM;
    }
    memset(selinux_module->selinux_te_file, 0, len);
    // te
    strcpy(selinux_module->selinux_te_file, selinux_module->selinux_rules_dir);
    strcat(selinux_module->selinux_te_file, selinux_module->id);
    strcat(selinux_module->selinux_te_file, TE_EXTENSION);

    selinux_module->selinux_fc_file = malloc(len);
    if (!selinux_module->selinux_fc_file) {
        ERROR("malloc selinux_fc_file");
        free_path_module_files(selinux_module);
        return -ENOMEM;
    }
    memset(selinux_module->selinux_fc_file, 0, len);
    // fc
    strcpy(selinux_module->selinux_fc_file, selinux_module->selinux_rules_dir);
    strcat(selinux_module->selinux_fc_file, selinux_module->id);
    strcat(selinux_module->selinux_fc_file, FC_EXTENSION);

    selinux_module->selinux_if_file = malloc(len);
    if (!selinux_module->selinux_if_file) {
        ERROR("malloc selinux_if_file");
        free_path_module_files(selinux_module);
        return -ENOMEM;
    }
    memset(selinux_module->selinux_if_file, 0, len);
    // if
    strcpy(selinux_module->selinux_if_file, selinux_module->selinux_rules_dir);
    strcat(selinux_module->selinux_if_file, selinux_module->id);
    strcat(selinux_module->selinux_if_file, IF_EXTENSION);

    selinux_module->selinux_pp_file = malloc(len);
    if (!selinux_module->selinux_pp_file) {
        ERROR("malloc selinux_pp_file");
        free_path_module_files(selinux_module);
        return -ENOMEM;
    }
    memset(selinux_module->selinux_pp_file, 0, len);
    // pp
    strcpy(selinux_module->selinux_pp_file, selinux_module->selinux_rules_dir);
    strcat(selinux_module->selinux_pp_file, selinux_module->id);
    strcat(selinux_module->selinux_pp_file, PP_EXTENSION);

    return 0;
}

/**
 * @brief Turns dash into underscore
 *
 * @param[in] s String to parse
 */
static void dash_to_underscore(char *s) {
    if (s) {
        while (*s) {
            if (*s == '-') {
                *s = '_';
            }
            s++;
        }
    }
}

/**
 * @brief Init selinux module
 *
 * @param[in] selinux_module selinux module handler to init
 * @param[in] id The id of application
 * @param[in] selinux_te_template_file some value or NULL for getting default
 * @param[in] selinux_if_template_file some value or NULL for getting default
 * @param[in] selinux_rules_dir some value or NULL for getting default
 * @return 0 in case of success or a negative -errno value
 */
static int init_selinux_module(selinux_module_t *selinux_module, const char *id, const char *selinux_te_template_file,
                               const char *selinux_if_template_file, const char *selinux_rules_dir) {
    if (!selinux_module) {
        ERROR("selinux_module undefined");
        return -EINVAL;
    } else if (!id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    memset(selinux_module, 0, sizeof(*selinux_module));

    selinux_module->selinux_rules_dir = get_selinux_rules_dir(selinux_rules_dir);
    selinux_module->selinux_te_template_file = get_selinux_te_template_file(selinux_te_template_file);
    selinux_module->selinux_if_template_file = get_selinux_if_template_file(selinux_if_template_file);

    selinux_module->id = strdup(id);
    if (selinux_module->id == NULL) {
        ERROR("strdup id");
        free_selinux_module(selinux_module);
        return -ENOMEM;
    }

    // id with underscore
    selinux_module->selinux_id = strdup(id);
    if (selinux_module->selinux_id == NULL) {
        ERROR("strdup selinux id");
        free_selinux_module(selinux_module);
        return -ENOMEM;
    }
    dash_to_underscore(selinux_module->selinux_id);

    int rc = generate_path_module_files(selinux_module);
    if (rc < 0) {
        ERROR("generate_path_module_files");
        free_selinux_module(selinux_module);
        return rc;
    }

    return 0;
}

/**
 * @brief Parse a line. ~APP~ turns into selinux_id and ~ID~ into id
 *
 * @param[in,out] line The line to parse
 * @param[in] id The id of the application
 * @param[in] selinux_id The selinux id of the application
 * @return 0 in case of success or a negative -errno value
 */
static int parse_line(const char *line, const char *id, const char *selinux_id) {
    if (!line) {
        ERROR("line undefined");
        return -EINVAL;
    } else if (!id) {
        ERROR("id undefined");
        return -EINVAL;
    } else if (!selinux_id) {
        ERROR("selinux_id undefined");
        return -EINVAL;
    }

    int rc = 0;

    char *pos_str;
    char after[MAX_LINE_SIZE_MODULE];

    // Replace ~ID~
    while ((pos_str = strstr(line, REPLACE_ID))) {
        strcpy(after, pos_str + strlen(REPLACE_ID));  // save overwrite data
        strcpy(pos_str, id);
        strcpy(pos_str + strlen(id), after);
    }

    // Replace ~APP~
    while ((pos_str = strstr(line, REPLACE_APP))) {
        strcpy(after, pos_str + strlen(REPLACE_APP));  // save overwrite data
        strcpy(pos_str, selinux_id);
        strcpy(pos_str + strlen(selinux_id), after);
    }

    return rc;
}

/**
 * @brief Transforms the template into a module
 *
 * @param[in] template The path of a selinux template
 * @param[in] module The path of the module
 * @param[in] id The id of the application
 * @param[in] selinux_id The selinux id of the application
 * @return 0 in case of success or a negative -errno value
 */
static int template_to_module(const char *template, const char *module, const char *id, const char *selinux_id) {
    if (!template) {
        ERROR("template undefined");
        return -EINVAL;
    } else if (!module) {
        ERROR("module undefined");
        return -EINVAL;
    } else if (!id) {
        ERROR("id undefined");
        return -EINVAL;
    } else if (!selinux_id) {
        ERROR("selinux_id undefined");
        return -EINVAL;
    }

    int rc = 0;
    char line[MAX_LINE_SIZE_MODULE];

    FILE *f_template = fopen(template, "r");

    if (f_template == NULL) {
        rc = -errno;
        ERROR("fopen %s %m", template);
        goto ret;
    }

    FILE *f_module = fopen(module, "w");

    if (f_module == NULL) {
        rc = -errno;
        ERROR("fopen %s %m", module);
        goto error1;
    }

    while (fgets(line, MAX_LINE_SIZE_MODULE, f_template)) {
        rc = parse_line(line, id, selinux_id);
        if (rc < 0) {
            ERROR("parse_line");
            goto error2;
        }

        rc = fputs(line, f_module);
        if (rc < 0) {
            rc = -errno;
            ERROR("fputs %m");
            goto error2;
        }
    }

error2:
    rc = fclose(f_module);
    if (rc < 0) {
        rc = -errno;
        ERROR("fclose %m");
    }
error1:
    rc = fclose(f_template);
    if (rc < 0) {
        rc = -errno;
        ERROR("fclose %m");
    }
ret:
    return rc;
}

/**
 * @brief Generate the te file
 *
 * @param[in] selinux_te_template_file The selinux te template file
 * @param[in] selinux_te_file The selinux destination te file
 * @param[in] id The id of the application
 * @param[in] selinux_id The selinux id of the application
 * @return 0 in case of success or a negative -errno value
 */
static int generate_app_module_te(const char *selinux_te_template_file, const char *selinux_te_file, const char *id,
                                  const char *selinux_id) {
    if (!selinux_te_template_file) {
        ERROR("selinux_te_template_file undefined");
        return -EINVAL;
    } else if (!selinux_te_file) {
        ERROR("selinux_te_file undefined");
        return -EINVAL;
    } else if (!id) {
        ERROR("id undefined");
        return -EINVAL;
    } else if (!selinux_id) {
        ERROR("selinux_id undefined");
        return -EINVAL;
    }

    int rc = template_to_module(selinux_te_template_file, selinux_te_file, id, selinux_id);

    if (rc < 0) {
        ERROR("template_to_module");
        return rc;
    }

    return 0;
}

/**
 * @brief Generate the if file
 *
 * @param[in] selinux_if_template_file The selinux if template file
 * @param[in] selinux_if_file The selinux destination if file
 * @param[in] id The id of the application
 * @param[in] selinux_id The selinux id of the application
 * @return 0 in case of success or a negative -errno value
 */
static int generate_app_module_if(const char *selinux_if_template_file, const char *selinux_if_file, const char *id,
                                  const char *selinux_id) {
    if (!selinux_if_template_file) {
        ERROR("selinux_if_template_file undefined");
        return -EINVAL;
    } else if (!selinux_if_file) {
        ERROR("selinux_if_file undefined");
        return -EINVAL;
    } else if (!id) {
        ERROR("id undefined");
        return -EINVAL;
    } else if (!selinux_id) {
        ERROR("selinux_id undefined");
        return -EINVAL;
    }

    int rc = template_to_module(selinux_if_template_file, selinux_if_file, id, selinux_id);

    if (rc < 0) {
        ERROR("template_to_module");
        return rc;
    }

    return 0;
}

/**
 * @brief Generate the fc file
 *
 * @param[in] selinux_fc_file The selinux destination if file
 * @param[in] paths paths handler
 * @param[in] selinux_id The selinux id of the application
 * @return 0 in case of success or a negative -errno value
 */
static int generate_app_module_fc(const char *selinux_fc_file, const paths_t *paths, const char *selinux_id) {
    if (!selinux_fc_file) {
        ERROR("selinux_fc_file undefined");
        return -EINVAL;
    } else if (!paths) {
        ERROR("paths undefined");
        return -EINVAL;
    } else if (!selinux_id) {
        ERROR("selinux_id undefined");
        return -EINVAL;
    }
    int rc = 0;
    char line[MAX_LINE_SIZE_MODULE];

    FILE *f_module_fc = fopen(selinux_fc_file, "w");

    if (f_module_fc == NULL) {
        rc = -errno;
        ERROR("fopen %s %m", selinux_fc_file);
        goto ret;
    }

    for (size_t i = 0; i < paths->size; i++) {
        LOG("Add path %s with type %s", paths->paths[i].path, get_path_type_string(paths->paths[i].path_type));
        bool is_public = false;
        char *suffix = NULL;
        rc = get_path_type_info(paths->paths[i].path_type, &suffix, &is_public);

        if (rc < 0) {
            ERROR("get_path_type_info");
            goto error;
        }

        char *label = NULL;
        if (!is_public) {
            rc = generate_label(&label, selinux_id, suffix);

            if (rc < 0) {
                ERROR("generate_label");
                goto error;
            }
        } else {
            label = public_app;
        }

        // Size path + size label + size begin gen_context + size end gen_context
        if (strlen(paths->paths[i].path) + strlen(label) + 32 + 6 >= MAX_LINE_SIZE_MODULE) {
            ERROR("too long");
            free(label);
            label = NULL;
            goto error;
        }

        strcpy(line, paths->paths[i].path);
        strcat(line, " gen_context(system_u:object_r:");
        strcat(line, label);
        strcat(line, ",s0)\n");

        free(label);
        label = NULL;

        rc = fputs(line, f_module_fc);
        if (rc < 0) {
            rc = -errno;
            ERROR("fputs %m");
            goto error;
        }
    }

error:
    rc = fclose(f_module_fc);
    if (rc < 0) {
        rc = -errno;
        ERROR("Fail fclose %m");
    }
ret:
    return rc;
}

/**
 * @brief Generate te, if, fc files
 *
 * @param[in] selinux_module selinux module handler
 * @param[in] secure_app secure app handler
 * @return 0 in case of success or a negative -errno value
 */
static int generate_app_module_files(const selinux_module_t *selinux_module, const secure_app_t *secure_app) {
    if (!selinux_module) {
        ERROR("selinux_module undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_te_template_file) {
        ERROR("selinux_te_template_file undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_te_file) {
        ERROR("selinux_te_file undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_fc_file) {
        ERROR("selinux_fc_file undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_if_template_file) {
        ERROR("selinux_if_template_file undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_if_file) {
        ERROR("selinux_if_file undefined");
        return -EINVAL;
    } else if (!selinux_module->id) {
        ERROR("id undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_id) {
        ERROR("selinux_id undefined");
        return -EINVAL;
    } else if (!secure_app) {
        ERROR("secure_app undefined");
        return -EINVAL;
    }

    int rc = generate_app_module_te(selinux_module->selinux_te_template_file, selinux_module->selinux_te_file,
                                    selinux_module->id, selinux_module->selinux_id);
    if (rc < 0) {
        ERROR("generate_app_module_te");
        return rc;
    }

    rc = generate_app_module_if(selinux_module->selinux_if_template_file, selinux_module->selinux_if_file,
                                selinux_module->id, selinux_module->selinux_id);
    if (rc < 0) {
        ERROR("generate_app_module_if");
        if (remove_file(selinux_module->selinux_te_file) < 0) {
            ERROR("remove te file");
        }
        return rc;
    }

    rc = generate_app_module_fc(selinux_module->selinux_fc_file, &(secure_app->paths), selinux_module->selinux_id);
    if (rc < 0) {
        ERROR("generate_app_module_fc");
        if (remove_file(selinux_module->selinux_te_file) < 0) {
            ERROR("remove te file");
        }
        if (remove_file(selinux_module->selinux_if_file) < 0) {
            ERROR("remove if file");
        }
        return rc;
    }

    return 0;
}

/**
 * @brief Check te, fc, if file exists
 *
 * @param[in] selinux_module selinux module handler
 * @return true if all exist
 * @return false if not
 */
static bool check_app_module_files_exists(const selinux_module_t *selinux_module) {
    if (!check_file_exists(selinux_module->selinux_te_file))
        return false;
    if (!check_file_exists(selinux_module->selinux_fc_file))
        return false;
    if (!check_file_exists(selinux_module->selinux_if_file))
        return false;

    return true;
}

/**
 * @brief Remove app module files (te, fc, if)
 *
 * @param[in] selinux_module selinux module handler
 * @return 0 in case of success or a negative -errno value
 */
static int remove_app_module_files(const selinux_module_t *selinux_module) {
    if (!selinux_module) {
        ERROR("selinux_module undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_te_file) {
        ERROR("selinux_te_file undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_fc_file) {
        ERROR("selinux_fc_file undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_if_file) {
        ERROR("selinux_if_file undefined");
        return -EINVAL;
    }

    int rc = remove_file(selinux_module->selinux_te_file);
    if (rc < 0) {
        ERROR("remove_file");
        return rc;
    }

    rc = remove_file(selinux_module->selinux_if_file);
    if (rc < 0) {
        ERROR("remove_file");
        return rc;
    }

    rc = remove_file(selinux_module->selinux_fc_file);
    if (rc < 0) {
        ERROR("remove_file");
        return rc;
    }

    return 0;
}

/**
 * @brief Remove pp file
 *
 * @param[in] selinux_module selinux module handler
 * @return 0 in case of success or a negative -errno value
 */
static int remove_pp_files(const selinux_module_t *selinux_module) {
    if (!selinux_module) {
        ERROR("selinux_module undefined");
        return -EINVAL;
    } else if (!selinux_module->selinux_pp_file) {
        ERROR("selinux_pp_file undefined");
        return -EINVAL;
    }

    int rc = remove_file(selinux_module->selinux_pp_file);
    if (rc < 0) {
        ERROR("remove_file");
        return rc;
    }
    return 0;
}

/**
 * @brief Destroy semanage handle and content
 *
 * @param[in] semanage_handle semanage_handle handler
 * @return 0 in case of success or a negative -errno value
 */
static int destroy_semanage_handle(semanage_handle_t *semanage_handle) {
    if (!semanage_handle) {
        ERROR("semanage_handle undefined");
        return -EINVAL;
    }
    int rc = 0;

    if (semanage_is_connected(semanage_handle)) {
        rc = semanage_disconnect(semanage_handle);
        if (rc < 0) {
            ERROR("semanage_disconnect");
        }
    }
    semanage_handle_destroy(semanage_handle);

    return rc;
}

/**
 * @brief Create a semanage handle
 *
 * @param[out] semanage_handle pointer semanage_handle handler
 * @return 0 in case of success or a negative -errno value
 */
static int create_semanage_handle(semanage_handle_t **semanage_handle) {
    int rc = 0;
    *semanage_handle = semanage_handle_create();

    if (semanage_handle == NULL) {
        ERROR("semanage_handle_create");
        return -1;
    }

    semanage_set_create_store(*semanage_handle, 1);

    rc = semanage_connect(*semanage_handle);
    if (rc < 0) {
        ERROR("semanage_connect");
        if (destroy_semanage_handle(*semanage_handle) < 0) {
            ERROR("destroy_semanage_handle");
        }
        return rc;
    }

    rc = semanage_set_default_priority(*semanage_handle, 400);
    if (rc != 0) {
        ERROR("semanage_set_default_priority");
        if (destroy_semanage_handle(*semanage_handle) < 0) {
            ERROR("destroy_semanage_handle");
        }
        return rc;
    }

    return 0;
}

/**
 * @brief Install selinux module
 *
 * @param[in] semanage_handle semanage_handle handler
 * @param[in] selinux_pp_file Path of the pp file
 * @return 0 in case of success or a negative -errno value
 */
static int install_module(semanage_handle_t *semanage_handle, const char *selinux_pp_file) {
    if (!semanage_handle) {
        ERROR("semanage_handle undefined");
        return -EINVAL;
    }

    int rc = semanage_module_install_file(semanage_handle, selinux_pp_file);
    if (rc < 0) {
        ERROR("semanage_module_install_file");
        return rc;
    }

    rc = semanage_commit(semanage_handle);
    if (rc < 0) {
        ERROR("semanage_commit");
        return rc;
    }
    return 0;
}

/**
 * @brief Remove module in the policy
 *
 * @param[in] semanage_handle semanage handle handler
 * @param[in] module_name Module name in the selinux policy
 * @return 0 in case of success or a negative -errno value
 */
static int remove_module(semanage_handle_t *semanage_handle, const char *module_name) {
    if (!semanage_handle) {
        ERROR("semanage_handle undefined");
        return -EINVAL;
    }

    char *module_name_ = strdup(module_name);  // semanage_module_remove take no const module name
    if (module_name == NULL) {
        ERROR("strdup");
        return -ENOMEM;
    }

    int rc = semanage_module_remove(semanage_handle, module_name_);
    free(module_name_);
    if (rc < 0) {
        ERROR("semanage_module_remove");
        return rc;
    }

    rc = semanage_commit(semanage_handle);
    if (rc < 0) {
        ERROR("semanage_commit");
        return rc;
    }
    return 0;
}

/**
 * @brief Free semanage_module_info_list
 *
 * @param[in] semanage_handle semanage_handle handler
 * @param[in] semanage_module_info_list semanage_module_info_list handler
 * @param[in] semanage_module_info_len semanage_module_info_len handler
 */
static void free_module_info_list(semanage_handle_t *semanage_handle, semanage_module_info_t *semanage_module_info_list,
                                  int semanage_module_info_len) {
    semanage_module_info_t *semanage_module_info = NULL;
    for (int i = 0; i < semanage_module_info_len; i++) {
        semanage_module_info = semanage_module_list_nth(semanage_module_info_list, i);
        semanage_module_info_destroy(semanage_handle, semanage_module_info);
    }
    free(semanage_module_info_list);
}

/**
 * @brief Check module in selinux policy
 *
 * @param[in] semanage_handle semanage_handle handler
 * @param[in] id name of the module
 * @return 1 if exists, 0 if not or a negative -errno value
 */
static int check_module(semanage_handle_t *semanage_handle, const char *id) {
    if (!semanage_handle) {
        ERROR("semanage_handle undefined");
        return -EINVAL;
    } else if (!id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    semanage_module_info_t *semanage_module_info = NULL;
    semanage_module_info_t *semanage_module_info_list = NULL;
    int semanage_module_info_len = 0;

    int rc = semanage_module_list(semanage_handle, &semanage_module_info_list, &semanage_module_info_len);
    if (rc < 0) {
        ERROR("semanage_module_list");
        goto end;
    }

    for (int i = 0; i < semanage_module_info_len; i++) {
        semanage_module_info = semanage_module_list_nth(semanage_module_info_list, i);
        const char *module_name = NULL;
        rc = semanage_module_info_get_name(semanage_handle, semanage_module_info, &module_name);
        if (rc < 0) {
            ERROR("semanage_module_info_get_name");
            break;
        }
        if (!strcmp(module_name, id)) {
            rc = 1;
            break;
        }
    }
end:
    free_module_info_list(semanage_handle, semanage_module_info_list, semanage_module_info_len);
    return rc;
}

/**********************/
/*** PUBLIC METHODS ***/
/**********************/

/* see selinux-template.h */
const char *get_selinux_te_template_file(const char *value) {
    return value ?: secure_getenv("SELINUX_TE_TEMPLATE_FILE") ?: default_selinux_te_template_file;
}

/* see selinux-template.h */
const char *get_selinux_if_template_file(const char *value) {
    return value ?: secure_getenv("SELINUX_IF_TEMPLATE_FILE") ?: default_selinux_if_template_file;
}

/* see selinux-template.h */
const char *get_selinux_rules_dir(const char *value) {
    return value ?: secure_getenv("SELINUX_RULES_DIR") ?: DEFAULT_SELINUX_RULES_DIR;
}

/* see selinux-template.h */
int create_selinux_rules(const secure_app_t *secure_app, const char *selinux_te_template_file,
                         const char *selinux_if_template_file, const char *selinux_rules_dir) {
    if (!secure_app) {
        ERROR("secure_app undefined");
        return -EINVAL;
    } else if (!secure_app->id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    selinux_module_t selinux_module;
    int rc = init_selinux_module(&selinux_module, secure_app->id, selinux_te_template_file, selinux_if_template_file,
                                 selinux_rules_dir);
    if (rc < 0) {
        ERROR("init_selinux_module");
        goto ret;
    }

    semanage_handle_t *semanage_handle;
    rc = create_semanage_handle(&semanage_handle);
    if (rc < 0) {
        ERROR("create_semanage_handle");
        goto end;
    }

    // Generate files
    rc = generate_app_module_files(&selinux_module, secure_app);
    if (rc < 0) {
        ERROR("generate_app_module_files");
        goto end2;
    }

    // fc, if, te generated
    rc = launch_compile();
    if (rc < 0) {
        ERROR("launch_compile");
        goto error3;
    }

    // pp generated

    rc = install_module(semanage_handle, selinux_module.selinux_pp_file);

    if (rc < 0) {
        ERROR("install_module");
        goto error4;
    }

    goto end2;

error4:
    remove_pp_files(&selinux_module);
error3:
    remove_app_module_files(&selinux_module);
end2:
    rc = destroy_semanage_handle(semanage_handle);
    if (rc < 0) {
        ERROR("destroy_semanage_handle");
    }
end:
    free_selinux_module(&selinux_module);
ret:
    return rc;
}

/* see selinux-template.h */
int check_module_files_exist(const secure_app_t *secure_app, const char *selinux_rules_dir) {
    if (!secure_app) {
        ERROR("secure_app undefined");
        return -EINVAL;
    } else if (!secure_app->id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    selinux_module_t selinux_module;
    int rc = init_selinux_module(&selinux_module, secure_app->id, NULL, NULL, selinux_rules_dir);
    if (rc < 0) {
        ERROR("init_selinux_module");
        return rc;
    }

    rc = check_app_module_files_exists(&selinux_module);

    free_selinux_module(&selinux_module);

    return rc;
}

/* see selinux-template.h */
int check_module_in_policy(const secure_app_t *secure_app) {
    if (!secure_app) {
        ERROR("secure_app undefined");
        return -EINVAL;
    } else if (!secure_app->id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    semanage_handle_t *semanage_handle;
    int rc = create_semanage_handle(&semanage_handle);
    if (rc < 0) {
        ERROR("create_semanage_handle");
        return rc;
    }

    rc = check_module(semanage_handle, secure_app->id);
    if (rc < 0) {
        ERROR("check_module");
    }

    if (destroy_semanage_handle(semanage_handle) < 0) {
        ERROR("destroy_semanage_handle");
    }

    return rc;
}

/* see selinux-template.h */
int remove_selinux_rules(const secure_app_t *secure_app, const char *selinux_rules_dir) {
    if (!secure_app) {
        ERROR("secure_app undefined");
        return -EINVAL;
    } else if (!secure_app->id) {
        ERROR("id undefined");
        return -EINVAL;
    }

    // remove files
    selinux_module_t selinux_module;
    int rc = init_selinux_module(&selinux_module, secure_app->id, NULL, NULL, selinux_rules_dir);
    if (rc < 0) {
        ERROR("init_selinux_module");
        goto ret;
    }

    rc = remove_app_module_files(&selinux_module) + remove_pp_files(&selinux_module);
    if (rc < 0) {
        ERROR("remove files modules");
        goto end;
    }

    // remove module in policy
    semanage_handle_t *semanage_handle;
    rc = create_semanage_handle(&semanage_handle);
    if (rc < 0) {
        ERROR("create_semanage_handle");
        goto end;
    }
    rc = remove_module(semanage_handle, secure_app->id);
    if (rc < 0) {
        ERROR("remove_module");
        goto end2;
    }

    goto end2;

end2:
    rc = destroy_semanage_handle(semanage_handle);
    if (rc < 0) {
        ERROR("destroy_semanage_handle");
    }
end:
    free_selinux_module(&selinux_module);
ret:
    return rc;
}