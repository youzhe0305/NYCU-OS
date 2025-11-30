#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <iostream>

using namespace std;

const char* tar_filename = "test.tar"; // tar file name
struct PosixHeader {
    char name[100];
    char mode[8];
    char uid[8]; // user id
    char gid[8]; // group id
    char size[12]; // content size
    char mtime[12]; // modification time
    char chksum[8]; // checksum
    char typeflag; // file type (0: regular, 5: directory, 2: symlink)
    char linkname[100]; // link target (for symlink)
    char magic[6]; // "ustar" signature => judge if legal
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8]; // device number
    char devminor[8]; // device number
    char prefix[155]; // if name is too long, store here
    char padding[12];
};

class FileNode{ // store file arichtecture as a tree
public:
    char name[256];
    size_t size;
    int mode; // mode include file type and permission
    int type;
    uid_t uid; 
    gid_t gid;
    time_t mtime;
    size_t offset;
    char link_target[256];
    vector<FileNode*> childs;

    FileNode(){
        memset(name, 0, sizeof(name));
        size = 0;
        mode = 0;
        type = 0;
        offset = 0;
        memset(link_target, 0, sizeof(link_target));
        childs.clear();
    }

    static vector<string> split_path(const char* path) {
        vector<string> result;
        stringstream ss(path);
        string item;
        while (getline(ss, item, '/')) {
            if (!item.empty()) result.push_back(item);
        }
        return result;
    }

    // input the relative path take this node as root
    // ex: for node / , input "dir1/file.txt"
    // ex: for node /dir1, input "file.txt" or "dir2/file2.txt"
    FileNode* find_node(const char* path) {
        if (!path || strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
            return this;
        }

        vector<string> parts = split_path(path);

        FileNode* curr = this;

        for (const string& part : parts) {
            FileNode* found = NULL;
            for (auto child : curr->childs) {
                if (strcmp(child->name, part.c_str()) == 0) {
                    found = child;
                    break;
                }
            }

            if (!found) return NULL; // no such node in subtree which root is curr

            curr = found;
        }

        return curr;
    }

    void print_tree(int depth = 0) {
        for (int i = 0; i < depth; ++i) {
            cout << "  ";
        }
        cout << name << " (size: " << size << ", mode: " << oct << mode << dec << ")\n";
        for (auto child : childs) {
            child->print_tree(depth + 1);
        }
    }

};
FileNode* root = new FileNode(); // file architecture tree root

int octal_to_int(char *oct) {
    return strtol(oct, NULL, 8);
}

// "dir1/file.txt" -> ["dir1", "file.txt"])
vector<string> split_path(const char* path) {
    vector<string> result;
    stringstream ss(path);
    string item;
    while (getline(ss, item, '/')) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

FileNode* get_parent_node(FileNode* root, const char* path) {
    vector<string> path_parts = split_path(path);
    FileNode* curr = root;

    for (size_t i = 0; i < path_parts.size() - 1; ++i) {
        string part = path_parts[i];
        
        FileNode* found = NULL;
        for (auto child : curr->childs) {
            if (strcmp(child->name, part.c_str()) == 0) {
                found = child;
                break;
            }
        }

        // some inter nodes haven't been created yet
        if (!found) { // create a defalut dir node for it
            found = new FileNode();
            strcpy(found->name, part.c_str());
            found->type = S_IFDIR;
            found->mode = S_IFDIR | 0755; // default permission
            found->uid = getuid();
            found->gid = getgid();
            curr->childs.push_back(found);
        }
        
        curr = found;
    }
    return curr;
}

void parse_tar_file(){

    FILE *fp = fopen(tar_filename, "rb");
    if (!fp) {
        perror("Cannot open test.tar");
        exit(1);
    }

    struct PosixHeader header;
    size_t cur = 0; // current byte offset

    strcpy(root->name, "/");
    root->type = S_IFDIR;
    root->mode = S_IFDIR | 0755;

    while (fread(&header, 512, 1, fp) > 0) {
        cur += 512; // read a header size once
        if (header.name[0] == '\0') break; // tar EOF

        FileNode* node = new FileNode();
        string full_path = header.name;        
        if (!full_path.empty() && full_path.back() == '/') {
            full_path.pop_back();
        }
        vector<string> parts = split_path(full_path.c_str());
        string filename = parts.back();
        strcpy(node->name, filename.c_str());

        int size = octal_to_int(header.size);
        int mode = octal_to_int(header.mode);
        node->size = size;
        node->offset = cur;
        node->uid = octal_to_int(header.uid);
        node->gid = octal_to_int(header.gid);
        node->mtime = octal_to_int(header.mtime);
        
        if(header.typeflag == '0'){ // regular file
            node->type = S_IFREG; // oct 0100000
            node->mode = S_IFREG | mode;
        }
        else if(header.typeflag == '5'){ // directory
            node->type = S_IFDIR; // oct 0040000
            node->mode = S_IFDIR | mode;
            node->size = 0;
        }
        else if(header.typeflag == '2'){ // symlink
            node->type = S_IFLNK; // oct 0120000
            node->mode = S_IFLNK | mode;
            node->size = 0;
            strcpy(node->link_target, header.linkname);
        }

        FileNode* parent = get_parent_node(root, header.name);

        FileNode* existing = NULL; // check if we have build a default node
        for (auto child : parent->childs) {
            if (strcmp(child->name, node->name) == 0) {
                existing = child;
                break;
            }
        }

        if (existing) { // default node exists, update its info
            existing->size = node->size;
            existing->mode = node->mode;
            existing->type = node->type;
            existing->offset = node->offset;
            existing->uid = node->uid;
            existing->gid = node->gid;
            existing->mtime = node->mtime;
            strcpy(existing->link_target, node->link_target);
            delete node;
        } else {
            parent->childs.push_back(node);
        }

        size_t content_blocks = (size + 511) / 512;
        fseek(fp, content_blocks * 512, SEEK_CUR);
        cur += content_blocks * 512; // move by at least 512 bytes (1 block)

    }
    fclose(fp);
}

int my_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    FileNode* node = root->find_node(path);
    if (node == NULL) {
        return -ENOENT;
    }

    if (node->type != S_IFDIR) {
        return -ENOTDIR;
    }

    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    for (auto child : node->childs) {
        filler(buffer, child->name, NULL, 0);
    }

    return 0;
}
int my_getattr(const char *path, struct stat *st) {
    memset(st, 0, sizeof(struct stat));

    FileNode* node = root->find_node(path);
    if (node == NULL) {
        return -ENOENT;
    }

    st->st_mode = node->mode;
    st->st_size = node->size;
    
    // if (node->type == S_IFDIR) {
    //     st->st_nlink = 2; // self and '.'
    // } else {
    //     st->st_nlink = 1;
    // }
    st->st_nlink = 0;

    st->st_uid = node->uid;
    st->st_gid = node->gid;
    
    st->st_mtime = node->mtime;
    st->st_atime = time(NULL);
    st->st_ctime = time(NULL);

    return 0; // success
}
int my_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    
    FileNode* node = root->find_node(path);
    if (node == NULL) {
        return -ENOENT;
    }


    if (offset >= node->size) {
        return 0; 
    }

    if (offset + size > node->size) {
        size = node->size - offset;
    }

    FILE *fp = fopen(tar_filename, "rb");
    if (fp == NULL) {
        return -errno; // 回傳系統錯誤碼
    }

    fseek(fp, node->offset + offset, SEEK_SET); 
    
    size_t bytes_read = fread(buffer, 1, size, fp);
    
    fclose(fp);

    return bytes_read;
}
int my_readlink(const char *path, char *buffer, size_t size) {
    FileNode* node = root->find_node(path);
    if (node == NULL) {
        return -ENOENT;
    }

    if (node->type != S_IFLNK) {
        return -EINVAL;
    }

    snprintf(buffer, size, "%s", node->link_target);

    return 0; // success
}

static struct fuse_operations op;
int main(int argc, char *argv[]) {

    memset(&op, 0, sizeof(op));
    op.getattr = my_getattr;
    op.readdir = my_readdir;
    op.read = my_read;
    op.readlink = my_readlink;
    
    // analyze tar for following functions
    parse_tar_file();

    root->print_tree();

    // forward the functions to FUSE
    return fuse_main(argc, argv, &op, NULL);
}
/*
g++ 112550069.cpp -o 112550069.out `pkg-config fuse --cflags --libs`
./112550069.out -f tarfs
*/