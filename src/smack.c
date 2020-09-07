/*
 * Copyright (C) 2020 IoT.bzh Company
 * Author: Arthur Guyader <arthur.guyader@iot.bzh>
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 *
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
 */

#include "smack.h"

#include <errno.h>
#include <linux/xattr.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>

#include "log.h"
#include "smack-label.h"
#include "smack-template.h"
#include "utils.h"

/***********************/
/*** PRIVATE METHODS ***/
/***********************/

/**
 * @brief Set smack attr on file
 *
 * @param[in] path the path of the file
 * @param[in] xattr name of the extended attribute
 * @param[in] value value of the extended attribute
 * @return 0 in case of success or a negative -errno value
 */
__nonnull() static int set_smack(const char *path, const char *xattr, const char *value) __wur {
    int rc = lsetxattr(path, xattr, value, strlen(value), 0);
    if (rc < 0) {
        rc = -errno;
        ERROR("lsetxattr('%s','%s','%s',%ld,%d) : %m", path, xattr, value, strlen(value), 0);
        return rc;
    }

    LOG("set %s=%s on %s", xattr, value, path);

    return 0;
}

/**
 * @brief Label file
 *
 * @param[in] path The path of the file
 * @param[in] label The label to set
 * @return 0 in case of success or a negative -errno value
 */
__nonnull() static int label_file(const char *path, const char *label) __wur {
    if (!check_file_exists(path)) {
        LOG("%s not exist", path);
        return -1;
    }

    int rc = set_smack(path, XATTR_NAME_SMACK, label);
    if (rc < 0) {
        ERROR("set_smack(%s,%s,%s)", path, XATTR_NAME_SMACK, label);
        return rc;
    }

    return 0;
}

/**
 * @brief Label a directory to be transmute
 *
 * @param[in] path The path of the directory
 * @return 0 in case of success or a negative -errno value
 */
__nonnull() static int label_dir_transmute(const char *path) __wur {
    if (!check_file_type(path, __S_IFDIR)) {
        LOG("%s not directory", path);
        return 0;
    }

    int rc = set_smack(path, XATTR_NAME_SMACKTRANSMUTE, "TRUE");
    if (rc < 0) {
        ERROR("set_smack(%s,%s,%s)", path, XATTR_NAME_SMACKTRANSMUTE, "TRUE");
        return rc;
    }

    return 0;
}

/**
 * @brief Label an executable file
 *
 * @param[in] path The path of the file
 * @param[in] label The label that will be used when exec
 * @return 0 in case of success or a negative -errno value
 */
__nonnull() static int label_exec(const char *path, const char *label) __wur {
    if (!check_file_type(path, __S_IFREG)) {
        LOG("%s not regular file", path);
        return 0;
    }

    if (!check_executable(path)) {
        ERROR("%s not executable", path);
        return 0;  // Check that it should not be restricted.
    }

    // remove :Exec
    char *label_no_exec = strdupa(label);
    if (!label_no_exec) {
        ERROR("strdupa");
        return -ENOMEM;
    }

    label_no_exec[strlen(label_no_exec) - strlen(suffix_exec)] = 0;

    int rc = set_smack(path, XATTR_NAME_SMACKEXEC, label_no_exec);
    if (rc < 0) {
        ERROR("set_smack(%s,%s,%s)", path, XATTR_NAME_SMACKEXEC, label);
        return rc;
    }

    return 0;
}

/**
 * @brief Label a file
 *
 * @param[in] path The path of the file
 * @param[in] label The label of the file
 * @param[in] is_executable The file is an executable
 * @param[in] is_transmute The directory is transmute
 * @return 0 in case of success or a negative -errno value
 */
__nonnull((1, 2)) static int label_path(const char *path, const char *label, int is_executable,
                                        int is_transmute) __wur {
    int rc = label_file(path, label);
    if (rc < 0) {
        ERROR("label file");
        return rc;
    }

    if (is_executable) {
        rc = label_exec(path, label);
        if (rc < 0) {
            ERROR("label exec");
            return rc;
        }
    }

    if (is_transmute) {
        rc = label_dir_transmute(path);
        if (rc < 0) {
            ERROR("label dir");
            return rc;
        }
    }

    return 0;
}

/**
 * @brief Apply smack on a path
 *
 * @param[in] path path handler
 * @param[in] id the id of the application
 * @return 0 in case of success or a negative -errno value
 */
__nonnull() static int smack_process_path(const path_t *path,
                                          path_type_definitions_t path_type_definitions[number_path_type]) __wur {
    int rc = label_path(path->path, path_type_definitions[path->path_type].label,
                        path_type_definitions[path->path_type].is_executable,
                        path_type_definitions[path->path_type].is_transmute);

    if (rc < 0) {
        ERROR("label_path");
        return rc;
    }

    return 0;
}

/**
 * @brief Apply smack on a secure app
 *
 * @param[in] secure_app secure app handler
 * @return 0 in case of success or a negative -errno value
 */
__nonnull() static int smack_process_paths(const secure_app_t *secure_app,
                                           path_type_definitions_t path_type_definitions[number_path_type]) __wur {
    for (size_t i = 0; i < secure_app->path_set.size; i++) {
        int rc = smack_process_path(secure_app->path_set.paths + i, path_type_definitions);
        if (rc < 0) {
            ERROR("smack_process_path((%s,%s),%s)", secure_app->path_set.paths[i].path,
                  get_path_type_string(secure_app->path_set.paths[i].path_type), secure_app->id);
            return rc;
        }
    }

    return 0;
}

/**********************/
/*** PUBLIC METHODS ***/
/**********************/

/* see smack.h */
int install_smack(const secure_app_t *secure_app) {
    path_type_definitions_t path_type_definitions[number_path_type] = {0};
    int rc = init_path_type_definitions(path_type_definitions, secure_app->id);
    if (rc < 0) {
        ERROR("init_path_type_definitions");
        goto ret;
    }

    rc = create_smack_rules(secure_app, path_type_definitions, NULL, NULL);
    if (rc < 0) {
        ERROR("create_smack_rules");
        goto end1;
    }

    rc = smack_process_paths(secure_app, path_type_definitions);
    if (rc < 0) {
        ERROR("smack_process_paths");
        goto error2;
    }

    LOG("install smack success");

    goto end1;

error2:
    if (remove_smack_rules(secure_app, NULL) < 0) {
        ERROR("remove_smack_rules");
    }
end1:
    free_path_type_definitions(path_type_definitions);
ret:
    return rc;
}

/* see smack.h */
int uninstall_smack(const secure_app_t *secure_app) {
    int rc = remove_smack_rules(secure_app, NULL);
    if (rc < 0) {
        ERROR("remove_app_rules : %d %s", rc, strerror(rc));
        return rc;
    }

    return 0;
}
