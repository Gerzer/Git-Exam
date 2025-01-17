//
//  valgit.c
//  valgit
//
//  Created by Gabriel Jacoby-Cooper on 12/29/22.
//

#include <dirent.h>
#include <ftw.h>
#include <sys/param.h>

#include "valgit.h"

#define FILE_INPUT "custom_validator_input.json"
#define FILE_URL "URL.txt"
#define DIRECTORY_CLONE "Clone"
#define DIRECTORY_CLONE_PUBLIC "Clone (Public)"

#define e_ret(expr) e_ret = expr; val_error_free(e_ret, &ptrs, &ptrs_git, &ptrs_json, &ptrs_python); if (e_ret < 0) return e_ret
#define e_ret_null(expr) e_ret = expr; val_error_free(e_ret, &ptrs, &ptrs_git, &ptrs_json, &ptrs_python); if (e_ret < 0) return NULL
#define e_ret_force() val_error_free(-1, &ptrs, &ptrs_git, &ptrs_json, &ptrs_python); return -1
#define e_ret_null_force() val_error_free(-1, &ptrs, &ptrs_git, &ptrs_json, &ptrs_python); return NULL

struct list_git_oid_t {
	git_oid oid;
	struct list_git_oid_t* prev;
};

const char* val_status_str(enum val_status_t status) {
	switch (status) {
	case VAL_STATUS_FAILURE:
		return "failure";
	case VAL_STATUS_INFORMATION:
		return "information";
	case VAL_STATUS_SUCCESS:
		return "success";
	case VAL_STATUS_WARNING:
		return "warning";
	default:
		return NULL;
	}
}

const char* val_error_message(int ret) {
	const char* message = strerror(errno);
	if (errno) {
		fprintf(stderr, "Error %d: %s\n", errno, message);
	} else {
		fprintf(stderr, "Error %d\n", errno);
	}
	return message;
}

const char* val_error_message_git(int ret) {
	const git_error* error = git_error_last();
	if (error) {
		fprintf(stderr, "Git error %d/%d: %s\n", ret, error->klass, error->message);
		return error->message;
	} else {
		return val_error_message(ret);
	}
}

const char* val_error_message_json(int ret) {
	const char* message = json_util_get_last_err();
	if (message) {
		fprintf(stderr, "JSON error %d: %s\n", ret, message);
	} else {
		message = val_error_message(ret);
	}
	return message;
}

void val_error_free(int ret, struct ptrs_t* ptrs, struct ptrs_git_t* ptrs_git, struct ptrs_json_t* ptrs_json, struct ptrs_python_t* ptrs_python) {
	if (ret < 0) {
		ptrs_free2(ptrs);
		ptrs_git_free2(ptrs_git);
		ptrs_json_free2(ptrs_json);
		ptrs_python_free2(ptrs_python);
	}
}

void val_error_exit(int ret, struct ptrs_t* ptrs, struct ptrs_git_t* ptrs_git, struct ptrs_json_t* ptrs_json, struct ptrs_python_t* ptrs_python, const char* error_message_get(int), const char* str_message_custom) {
	val_error_free(ret, ptrs, ptrs_git, ptrs_json, ptrs_python);
	if (ret < 0) {
		struct val_message_t message = {
			.status = VAL_STATUS_FAILURE,
			.message = "An unknown error occurred."
		};
		if (error_message_get) {
			const char* str_message = error_message_get(ret);
			if (str_message) {
				message.message = str_message;
			}
		} else if (str_message_custom) {
			fprintf(stderr, "Error %d: %s\n", ret, str_message_custom);
			message.message = str_message_custom;
		}
		json_object* json = json_create_val_failure(message);
		if (json) {
			json_object_to_file(FILE_RESULTS, json);
			json_object_put(json);
		}
		ret = git_libgit2_shutdown();
		if (ret < 0) {
			val_error_message_git(ret);
		}
		exit(ret);
	}
}

void ptrs_add2(void* ptr, struct ptrs_t* ptrs) {
	ptrs->ptrs = realloc(ptrs->ptrs, ++ptrs->len * sizeof(void*));
	ptrs->ptrs[ptrs->len - 1] = ptr;
}

void ptrs_free2(struct ptrs_t* ptrs) {
	for (unsigned int i = 0; i < ptrs->len; i++) {
		free(ptrs->ptrs[i]);
	}
	if (ptrs->ptrs) {
		free(ptrs->ptrs);
	}
	ptrs->len = 0;
}

void ptrs_git_add2(git_object* ptr_git, struct ptrs_git_t* ptrs_git) {
	ptrs_git->ptrs = realloc(ptrs_git->ptrs, ++ptrs_git->len * sizeof(git_object*));
	ptrs_git->ptrs[ptrs_git->len - 1] = ptr_git;
}

void ptrs_git_repository_set2(git_repository* repo, struct ptrs_git_t* ptrs_git) {
	ptrs_git->repo = repo;
}

void ptrs_git_free2(struct ptrs_git_t* ptrs_git) {
	for (unsigned int i = 0; i < ptrs_git->len; i++) {
		git_object* ptr_git = ptrs_git->ptrs[i];
		switch (git_object_type(ptr_git)) {
		case GIT_OBJECT_INVALID:
			break;
		case GIT_OBJECT_COMMIT:
			git_commit_free((git_commit*) ptr_git);
			break;
		case GIT_OBJECT_TREE:
			git_tree_free((git_tree*) ptr_git);
			break;
		case GIT_OBJECT_BLOB:
			git_blob_free((git_blob*) ptr_git);
			break;
		case GIT_OBJECT_TAG:
			git_tag_free((git_tag*) ptr_git);
			break;
		case GIT_OBJECT_OFS_DELTA:
		case GIT_OBJECT_REF_DELTA:
		case GIT_OBJECT_ANY:
			git_object_free(ptr_git);
			break;
		}
	}
	if (ptrs_git->ptrs) {
		free(ptrs_git->ptrs);
	}
	if (ptrs_git->repo) {
		git_repository_free(ptrs_git->repo);
		ptrs_git->repo = NULL;
	}
	ptrs_git->len = 0;
}

void ptrs_json_add2(json_object* ptr_json, struct ptrs_json_t* ptrs_json) {
	ptrs_json->ptrs = realloc(ptrs_json->ptrs, ++ptrs_json->len * sizeof(json_object*));
	ptrs_json->ptrs[ptrs_json->len - 1] = ptr_json;
}

void ptrs_json_free2(struct ptrs_json_t* ptrs_json) {
	for (unsigned int i = 0; i < ptrs_json->len; i++) {
		json_object_put(ptrs_json->ptrs[i]);
	}
	if (ptrs_json->ptrs) {
		free(ptrs_json->ptrs);
	}
	ptrs_json->len = 0;
}

void ptrs_python_add2(PyObject* ptr_python, struct ptrs_python_t* ptrs_python) {
	ptrs_python->ptrs = realloc(ptrs_python->ptrs, ++ptrs_python->len * sizeof(PyObject*));
	ptrs_python->ptrs[ptrs_python->len - 1] = ptr_python;
}

void ptrs_python_free2(struct ptrs_python_t* ptrs_python) {
	for (unsigned int i = 0; i < ptrs_python->len; i++) {
		Py_DecRef(ptrs_python->ptrs[i]);
	}
	if (ptrs_python->ptrs) {
		free(ptrs_python->ptrs);
	}
	ptrs_python->len = 0;
}

int git_nth_commit_oid(git_oid* oid, bool* first, unsigned int n, const unsigned int pathv[], unsigned int pathc, const char* path_start, const git_reference* ref, git_repository* repo) {
	e_init();
	
	if (first) {
		// Assume by default that the found commit won’t be a source in the commit graph
		*first = false;
	}
	
	git_oid oid_commit;
	git_oid oid_commit_parent;
	
	// Get the name that’s associated with the given reference
	const char* name = git_reference_name(ref);
	e_ret(git_reference_name_to_id(&oid_commit_parent, repo, name));
	
	// Get the original commit
	git_commit* commit_orig;
	e_ret(git_commit_lookup(&commit_orig, repo, &oid_commit_parent));
	
	// Check the parent count of the original commit
	unsigned int parentcount_parent = git_commit_parentcount(commit_orig);
	git_commit_free(commit_orig);
	if (parentcount_parent == 0) {
		*oid = oid_commit_parent;
		if (first) {
			*first = true;
		}
		return 0;
	}
	
	// Initialize the list of commit OIDs
	struct list_git_oid_t oid_list = {
		.oid = oid_commit_parent,
		.prev = NULL
	};
	struct list_git_oid_t* tail = &oid_list;
	
	unsigned int i = pathc;
	do {
		oid_commit = oid_commit_parent;
		
		// Set the parent commit’s OID
		git_commit* commit;
		e_ret(git_commit_lookup(&commit, repo, &oid_commit));
		
		// Check the commit’s message
		if (path_start) {
			const char* commit_message = git_commit_message(commit);
			const size_t end = MAX(strcspn(commit_message, "\f\n\r"), 1);
			char commit_message_own[end + 1];
			for (unsigned long int i = 0; i <= end; i++) {
				commit_message_own[i] = '\0';
			}
			strncpy(commit_message_own, commit_message, end);
			if (strncmp(path_start, commit_message, 8) == 0) {
				i--;
			}
		}
		
		const git_oid* ptr_oid_commit_parent = git_commit_parent_id(commit, i >= 0 && i < pathc ? pathv[i] : 0);
		git_commit_free(commit);
		if (!ptr_oid_commit_parent) {
			e_ret_force();
		}
		oid_commit_parent = *ptr_oid_commit_parent;
		
		// Set the parent commit’s parent count
		git_commit* commit_parent;
		e_ret(git_commit_lookup(&commit_parent, repo, &oid_commit_parent));
		parentcount_parent = git_commit_parentcount(commit_parent);
		git_commit_free(commit_parent);
		
		struct list_git_oid_t* next = malloc(sizeof(struct list_git_oid_t));
		next->oid = oid_commit_parent;
		next->prev = tail;
		tail = next;
		ptrs_add(tail);
	} while (parentcount_parent > 0);
	
	// Traverse the list of commit OIDs
	struct list_git_oid_t* current = tail;
	for (unsigned int i = 0; i < n; i++) {
		if (!current->prev) {
			// Zero out the OID as a signal that the commit couldn’t be found
			for (unsigned int j = 0; j < GIT_OID_RAWSZ; j++) {
				oid->id[j] = '\0';
			}
			
			free_all();
			return 0; // We still return 0 so that the caller can handle the situation separately from a “regular” unexpected error.
		}
		current = current->prev;
	}
	*oid = current->oid;
	
	e_str = NULL;
	free_all();
	return 0;
}

json_object* json_create_val_success(double score, const struct val_message_t messagesv[], unsigned short int messagesc) {
	e_init();
	
	if (score < 0 || score > 1) {
		fprintf(stderr, "Invalid score value\n");
		free_all();
		return NULL;
	}
	
	// Create the root object
	json_object* object_root = json_object_new_object();
	if (!object_root) {
		e_ret_null_force();
	}
	ptrs_json_add(object_root);
	
	// Create a “status” string and add it to the root object
	json_object* string_status = json_object_new_string("success");
	e_ret_null(json_object_object_add(object_root, "status", string_status));
	
	// Create a “data” object
	json_object* object_data = json_object_new_object();
	ptrs_json_add(object_data);
	
	// Create a “score” double and add it to the “data” object
	json_object* double_score = json_object_new_double(score);
	e_ret_null(json_object_object_add(object_data, "score", double_score));
	
	if (messagesc == 1) {
		// Create a “message” string and add it to the “data” object
		json_object* string_message = json_object_new_string(messagesv[0].message);
		if (!string_message) {
			e_ret_null_force();
		}
		e_ret_null(json_object_object_add(object_data, "message", string_message));
		
		// Create a “status” string and add it to the “data” object
		const char* status_str = val_status_str(messagesv[0].status);
		if (!status_str) {
			fprintf(stderr, "Unknown message status\n");
			free_all();
			return NULL;
		}
		json_object* string_status = json_object_new_string(status_str);
		if (!string_status) {
			e_ret_null_force();
		}
		e_ret_null(json_object_object_add(object_data, "status", string_status));
	} else {
		// Create a “message” array
		json_object* array_message = json_object_new_array();
		if (!array_message) {
			e_ret_null_force();
		}
		ptrs_json_add(array_message);
		
		for (unsigned short int i = 0; i < messagesc; i++) {
			// Create a message object
			json_object* object_message = json_object_new_object();
			if (!object_message) {
				e_ret_null_force();
			}
			ptrs_json_add(object_message);
			
			// Create a “message” string and add it to the message object
			json_object* string_message = json_object_new_string(messagesv[i].message);
			if (!string_message) {
				e_ret_null_force();
			}
			e_ret_null(json_object_object_add(object_message, "message", string_message));
			
			// Create a “status” string and add it to the message object
			const char* status_str = val_status_str(messagesv[i].status);
			if (!status_str) {
				fprintf(stderr, "Unknown message status\n");
				free_all();
				return NULL;
			}
			json_object* string_status = json_object_new_string(status_str);
			if (!string_status) {
				e_ret_null_force();
			}
			e_ret_null(json_object_object_add(object_message, "status", string_status));
			
			// Add the message object to the “message” array
			e_ret_null(json_object_array_add(array_message, object_message));
			json_object_get(object_message); // free_all() will release object_message, so we retain it first so that it isn’t deallocated before we return to the caller.
		}
		
		// Add the “message” array to the “data” object
		e_ret_null(json_object_object_add(object_data, "message", array_message));
		json_object_get(array_message); // free_all() will release array_message, so we retain it first so that it isn’t deallocated before we return to the caller.
	}
	
	// Add the “data” object to the root object
	e_ret_null(json_object_object_add(object_root, "data", object_data));
	json_object_get(object_data); // free_all() will release object_data, so we retain it first so that it isn’t deallocated before we return to the caller.
	
	json_object_get(object_root); // free_all() will release object_root, so we retain it first so that it isn’t deallocated before we return to the caller.
	free_all();
	return object_root;
}

json_object* json_create_val_failure(struct val_message_t message) {
	e_init();
	
	if (message.status != VAL_STATUS_FAILURE) {
		fprintf(stderr, "Invalid message status\n");
		free_all();
		return NULL;
	}
	
	// Create the root object
	json_object* object_root = json_object_new_object();
	if (!object_root) {
		fprintf(stderr, "Failed to create root JSON object\n");
		free_all();
		return NULL;
	}
	ptrs_json_add(object_root);
	
	// Create a “status” string and add it to the root object
	json_object* string_status = json_object_new_string("failure");
	if (!string_status) {
		e_ret_null_force();
	}
	e_ret_null(json_object_object_add(object_root, "status", string_status));
	
	// Create a “message” string and add it to the root object
	json_object* string_message = json_object_new_string(message.message);
	if (!string_message) {
		e_ret_null_force();
	}
	e_ret_null(json_object_object_add(object_root, "message", string_message));
	
	json_object_get(object_root); // free_all() will release object_root, so we retain it first so that it isn’t deallocated before we return to the caller.
	free_all();
	return object_root;
}

char* val_python_result(const char* eval, struct val_python_context_t context, bool* eval_failed) {
	e_init();
	
	*eval_failed = false;
	
	PyObject* object = PyRun_String(eval, Py_eval_input, context.globals, context.locals);
	char* result = NULL;
	if (object) {
		PyObject* unicode = PyObject_Str(object);
		if (!unicode) {
			Py_DecRef(object);
			e_ret_null_force();
		}
		
		Py_ssize_t size;
		const char* string = PyUnicode_AsUTF8AndSize(unicode, &size);
		if (!string) {
			fprintf(stderr, "The Python evaluation result is not a string\n");
			*eval_failed = true;
			Py_DecRef(unicode);
			Py_DecRef(object);
			free_all();
			return NULL;
		}
		result = malloc(size + sizeof(char)); // We add the size of an additonal char to account for the null terminator.
		if (!result) {
			fprintf(stderr, "Couldn’t allocate memory on the heap to hold the Python evaluation result string\n");
			Py_DecRef(unicode);
			Py_DecRef(object);
			e_ret_null_force();
		}
		strcpy(result, string); // string will be deallocated automatically, so we must copy its memory into a buffer that we own directly.
		Py_DecRef(unicode);
		Py_DecRef(object);
	} else {
		fprintf(stderr, "Couldn’t evaluate the Python string\n");
		*eval_failed = true;
	}
	
	free_all();
	return result;
}

int val_python_result_file(const char* filename, struct val_python_context_t context, int start) {
	e_init();
	
	FILE* fp = fopen(filename, "r");
	if (!fp) {
		fprintf(stderr, "Failed to open Python file: %s\n", filename);
		e_ret_force();
	}
	PyObject* temp = PyRun_File(fp, filename, start, context.globals, context.globals);
	if (!temp) {
		fprintf(stderr, "Couldn’t evaluate the Python file\n");
		fclose(fp);
		e_ret_force();
	}
	Py_DecRef(temp);

	fclose(fp); // There’s nothing to do differently if fclose fails, so we don’t bother to capture its return value.
	
	free_all();
	return 0;
}

char* val_python_result_new(const char* eval, const char* filename, struct val_python_context_t* const context, bool* eval_failed) {
	e_init();
	
	*eval_failed = false;
	
	PyObject* module_obj = PyModule_New("module");
	if (!module_obj) {
		fprintf(stderr, "Couldn’t create the Python module object\n");
		e_ret_null_force();
	}
	if (context) {
		context->module_obj = module_obj;
	} else {
		ptrs_python_add(module_obj);
	}
	
	PyModule_AddStringConstant(module_obj, "__file__", "");
	
	PyObject* globals = PyDict_New();
	if (!globals) {
		fprintf(stderr, "Couldn’t create the Python globals dictionary\n");
		e_ret_null_force();
	}
	if (context) {
		context->globals = globals;
	} else {
		ptrs_python_add(globals);
	}
	
	PyObject* locals = PyModule_GetDict(module_obj);
	if (!locals) {
		fprintf(stderr, "Couldn’t get the Python locals dictionary\n");
		e_ret_null_force();
	}
	Py_IncRef(locals); // The Python runtime will automatically release locals, so we need to retain it ourselves to maintain ownership.
	if (context) {
		context->locals = locals;
	} else {
		ptrs_python_add(locals);
	}
	
	struct val_python_context_t context_actual;
	if (context) {
		context_actual = *context;
	} else {
		context_actual = (struct val_python_context_t) {
			.module_obj = module_obj,
			.globals = globals,
			.locals = locals
		};
	}
	e_ret_null(val_python_result_file(filename, context_actual, Py_file_input));
	char* result;
	if (eval) {
		result = val_python_result(eval, context_actual, eval_failed);
	} else {
		result = calloc(1, sizeof(char));
		*result = '\0';
	}
	
	free_all();
	return result;
}

void val_python_free(struct val_python_context_t context) {
	Py_DecRef(context.module_obj);
	Py_DecRef(context.globals);
	Py_DecRef(context.locals);
}

int val_remove(const char* path, const struct stat* stat_ptr, int flag, struct FTW* ftw_ptr) {
	return remove(path);
}

void val_try_remove_dir(const char* path) {
	DIR* dp = opendir(path);
	if (dp) {
		closedir(dp);
		fprintf(stderr, "Found existing directory “%s”; removing…\n", path);
		#ifdef OPEN_MAX
		int depth = OPEN_MAX;
		#else
		int depth = FOPEN_MAX;
		#endif
		nftw(path, val_remove, depth, FTW_PHYS | FTW_MOUNT | FTW_DEPTH);
	}
}

struct val_git_clone_payload_t* val_git_clone_payload_new(void) {
	struct val_git_clone_payload_t* payload = malloc(sizeof(struct val_git_clone_payload_t));
	payload->halt = false;
	payload->error = VAL_GIT_CLONE_ERROR_NONE;
	return payload;
}

int val_git_credential_acquire(git_credential** out, const char* url, const char* username_from_url, unsigned int allowed_types, void* payload) {
	*out = NULL;
	
	struct val_git_clone_payload_t* clone_payload = (struct val_git_clone_payload_t*) payload;
	if (clone_payload->halt) {
		return 1;
	} else {
		fprintf(stderr, "The server asked for one of the following credentials:\n");
		if ((allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) == GIT_CREDENTIAL_USERPASS_PLAINTEXT) {
			fprintf(stderr, "- USERPASS_PLAINTEXT\n");
		}
		if ((allowed_types & GIT_CREDENTIAL_SSH_KEY) == GIT_CREDENTIAL_SSH_KEY) {
			fprintf(stderr, "- SSH_KEY\n");
		}
		if ((allowed_types & GIT_CREDENTIAL_SSH_CUSTOM) == GIT_CREDENTIAL_SSH_CUSTOM) {
			fprintf(stderr, "- SSH_CUSTOM\n");
		}
		if ((allowed_types & GIT_CREDENTIAL_DEFAULT) == GIT_CREDENTIAL_DEFAULT) {
			fprintf(stderr, "- DEFAULT\n");
		}
		if ((allowed_types & GIT_CREDENTIAL_SSH_INTERACTIVE) == GIT_CREDENTIAL_SSH_INTERACTIVE) {
			fprintf(stderr, "- SSH_INTERACTIVE\n");
		}
		if ((allowed_types & GIT_CREDENTIAL_USERNAME) == GIT_CREDENTIAL_USERNAME) {
			fprintf(stderr, "- USERNAME\n");
		}
		if ((allowed_types & GIT_CREDENTIAL_SSH_MEMORY) == GIT_CREDENTIAL_SSH_MEMORY) {
			fprintf(stderr, "- SSH_MEMORY\n");
		}
		if (allowed_types == 0) {
			fprintf(stderr, "boo hiss bad server\n");
		}
		
		clone_payload->halt = true;
		
		char* rcsid = val_input(VAL_INPUT_KEY_RCSID);
		if (!rcsid) {
			fprintf(stderr, "Couldn’t read RCS ID\n");
			clone_payload->error = VAL_GIT_CLONE_ERROR_NORCSID;
			return 1;
		}
		
		char publickey[strlen(rcsid) + 5];
		strcpy(publickey, rcsid);
		strcat(publickey, ".pub");
		
		char privatekey[strlen(rcsid) + 1];
		strcpy(privatekey, rcsid);
		
		FILE* fp;
		
		fp = fopen(publickey, "r");
		if (!fp) {
			fprintf(stderr, "Public key not found for RCS ID: %s\n", rcsid);
			clone_payload->error = VAL_GIT_CLONE_ERROR_NOPUBLICKEY;
			return 1;
		}
		fclose(fp);
		
		fp = fopen(privatekey, "r");
		if (!fp) {
			fprintf(stderr, "Private key not found for RCS ID: %s\n", rcsid);
			clone_payload->error = VAL_GIT_CLONE_ERROR_NOPRIVATEKEY;
			return 1;
		}
		fclose(fp);
		
		free(rcsid);
		
		return git_credential_ssh_key_new(out, username_from_url, publickey, privatekey, NULL);
	}
}

int val_git_clone(const char* url, bool* access_public, git_repository** repository_out) {
	e_init();
	
	fprintf(stderr, "Cloning student repository (%s)…\n", url);
	
	*access_public = false;
	*repository_out = NULL;
	
	val_try_remove_dir(DIRECTORY_CLONE);
	val_try_remove_dir(DIRECTORY_CLONE_PUBLIC);
	
	if (strlen(url) < 22) {
		fprintf(stderr, "Unexpectedly short clone URL: %s\n", url);
		free_all();
		return VAL_GIT_CLONE_ERROR_SHORTURL;
	}
	
	char* url_ssh;
	if (strncmp("https://github.com/", url, 19) == 0) { // The student submitted an HTTPS clone URL
		fprintf(stderr, "Clone URL uses HTTPS: %s\n", url);
		
		// Convert the HTTPS URL into the corresponding SSH URL
		url_ssh = calloc(16 + strlen(url + 19), sizeof(char));
		if (!url_ssh) {
			fprintf(stderr, "Failed to allocate memory for the converted SSH clone URL\n");
			fprintf(stderr, "Original HTTPS URL: %s\n", url);
			e_ret_force();
		}
		e_ret = snprintf(url_ssh, (16 + strlen(url + 19)) * sizeof(char), "%s%s", "git@github.com:", url + 19);
		if (e_ret < 0) {
			e_ret_force();
		}
	} else {
		url_ssh = calloc(strlen(url) + 1, sizeof(char));
		if (!url_ssh) {
			fprintf(stderr, "Failed to allocate memory for the SSH clone URL: %s\n", url);
			e_ret_force();
		}
		memcpy(url_ssh, url, (strlen(url) + 1) * sizeof(char));
	}
	ptrs_add(url_ssh);
	
	if (strncmp("git@github.com:", url_ssh, 15) != 0) {
		fprintf(stderr, "Computed clone URL doesn’t have “git@github.com:” as a prefix: %s\n", url);
		free_all();
		return VAL_GIT_CLONE_ERROR_INVALIDURL;
	}
	
	char url_https[20 + strlen(url_ssh + 15)];
	e_ret(snprintf(url_https, sizeof(url_https), "%s%s", "https://github.com/", url_ssh + 15));
	
	git_clone_options clone_options_public;
	e_ret(git_clone_options_init(&clone_options_public, GIT_CLONE_OPTIONS_VERSION));
	
	git_repository* repository_public;
	git_clone(&repository_public, url_https, DIRECTORY_CLONE_PUBLIC, &clone_options_public);
	if (repository_public) {
		git_repository_free(repository_public);
		fprintf(stderr, "Student repository is publicly accessible\n");
		fprintf(stderr, "Academic-integrity violation detected‼\n");
		*access_public = true;
		e_ret_force();
	}
	
	struct val_git_clone_payload_t* payload = val_git_clone_payload_new();
	if (!payload) {
		e_ret_force();
	}
	ptrs_add(payload);
	
	git_remote_callbacks remote_callbacks;
	e_ret(git_remote_init_callbacks(&remote_callbacks, GIT_REMOTE_CALLBACKS_VERSION));
	remote_callbacks.credentials = val_git_credential_acquire;
	remote_callbacks.payload = payload;
	
	git_clone_options clone_options;
	e_ret(git_clone_options_init(&clone_options, GIT_CLONE_OPTIONS_VERSION));
	
	git_fetch_options fetch_options;
	e_ret(git_fetch_options_init(&fetch_options, GIT_FETCH_OPTIONS_VERSION));
	fetch_options.callbacks = remote_callbacks;
	
	clone_options.fetch_opts = fetch_options;
	
	git_repository* repository;
	git_clone(&repository, url_ssh, DIRECTORY_CLONE, &clone_options);
	assert(payload->halt == true);
	*repository_out = repository;
	
	enum val_git_clone_error_t error = payload->error;
	free_all();
	return error;
}

char* val_input(enum val_input_key_t input_key) {
	e_init();
	
	const json_object* root = json_object_from_file(FILE_INPUT);
	if (!root) {
		fprintf(stderr, "Failed to get the root JSON object from the input file: %s\n", FILE_INPUT);
		e_ret_null_force();
	}
	ptrs_json_add(root);
	
	const char* input_key_str;
	switch (input_key) {
	case VAL_INPUT_KEY_PREFIX:
		input_key_str = "testcase_prefix";
		break;
	case VAL_INPUT_KEY_RCSID:
		input_key_str = "username";
		break;
	default:
		fprintf(stderr, "Invalid input key: %u\n", input_key);
		e_ret_null_force();
	}
	
	json_object* value = json_object_object_get(root, input_key_str); // json_object_object_get(obj, key) doesn’t retain the returned object, so there’s no need for us to add the pointer to the memory-management subsystem.
	if (!value) {
		fprintf(stderr, "Failed to get the value for the key “%s” from the input file\n", input_key_str);
		e_ret_null_force();
	}
	
	const char* value_str = json_object_get_string(value);
	if (!value_str) {
		fprintf(stderr, "Failed to convert the value JSON object for the key “%s” into a C-string\n", input_key_str);
		e_ret_null_force();
	}
	
	char* value_str_own = calloc(strlen(value_str) + 1, sizeof(char)); // We add 1 to account for the null terminator.
	if (!value_str_own) {
		fprintf(stderr, "Failed to allocate memory for storing the value for the key “%s”\n", input_key_str);
		e_ret_null_force();
	}
	strcpy(value_str_own, value_str); // value_str will be deallocated automatically, so we must copy its memory into a buffer that we own directly.
	
	free_all();
	return value_str_own;
}

FILE* val_url_file(void) {
	fprintf(stderr, "Opening URL file (%s)…\n", FILE_URL);
	return fopen(FILE_URL, "r");
}

char* val_file_str(FILE* fp) {
	e_init();
	
	e_ret_null(fseek(fp, 0, SEEK_END));
	long int size = ftell(fp);
	if (size < 0) {
		fprintf(stderr, "Failed to get file size\n");
		e_ret_null_force();
	}
	errno = 0;
	rewind(fp);
	if (errno) {
		fprintf(stderr, "Failed to rewind file\n");
		e_ret_null_force();
	}
	char* str = malloc(size + 1);
	str[0] = '\0';
	unsigned long int i = 0;
	while (!feof(fp)) {
		unsigned char next = fgetc(fp);
		switch (next) {
		case ' ':
		case '\n':
		case '\r':
		case '\t':
			continue;;
		}
		str[i++] = next;
	}
	str[i - 1] = '\0';
	
	free_all();
	return str;
}
