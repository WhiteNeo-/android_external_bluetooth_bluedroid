/******************************************************************************
 *
 *  Copyright (C) 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/mman.h>

#include "btif_config.h"
#include "btif_config_util.h"
#ifndef ANDROID_NDK
#define ANDROID_NDK
#endif
#include "tinyxml2.h"
#ifndef FALSE
#define TRUE 1
#define FALSE 0
#endif
#define LOG_TAG "btif_config_util"
extern "C" {
#include "btif_sock_util.h"
}
#include <stdlib.h>
#include <cutils/log.h>
#define info(fmt, ...)  ALOGI ("%s(L%d): " fmt,__FUNCTION__, __LINE__,  ## __VA_ARGS__)
#define debug(fmt, ...) ALOGD ("%s(L%d): " fmt,__FUNCTION__, __LINE__,  ## __VA_ARGS__)
#define warn(fmt, ...) ALOGW ("## WARNING : %s(L%d): " fmt "##",__FUNCTION__, __LINE__, ## __VA_ARGS__)
#define error(fmt, ...) ALOGE ("## ERROR : %s(L%d): " fmt "##",__FUNCTION__, __LINE__, ## __VA_ARGS__)
#define asrt(s) if(!(s)) ALOGE ("## %s assert %s failed at line:%d ##",__FUNCTION__, #s, __LINE__)

#define BLUEDROID_ROOT "Bluedroid"
#define BLUEDROID_NAME_TAG "Tag"
#define BLUEDROID_VALUE_TYPE "Type"
#define BLUEDROID_TAG_REMOTE_DEVICE "Remote Devices"

#define HID_SUB_CLASS      "020208"
#define HID_COUNTRY_CODE      "020308"
#define HID_VIRTUAL_CABLE      "020428"
#define HID_RECON_INNITIATE      "020528"
#define HID_REP_DSC_1      "020636"
#define HID_REP_DSC_2      "020635"
#define HID_SDP_DISABLE      "020828"
#define HID_BAT_POWER      "020928"
#define HID_REM_WAKE      "020A28"
#define HID_SUP_TIME      "020C09"
#define HID_NORM_CONN      "020D28"
#define HID_SSR_MAX_LAT      "020F09"
#define HID_SSR_MIN_TIM      "021009"
#define HID_VENDOR_ID      "020109"
#define HID_PRODUCT_ID      "020209"
#define HID_PRODUCT_VERSION      "020309"
#define HID_APP_ID_MOUSE      1
#define HID_APP_ID_KYB      2
#define HID_PAIRED_DEV_PRIORITY      100
#define HID_SSR_PARAM_INVALID    0xffff
#define HID_RPT_DSCR_HDR_LEN_1    10
#define HID_RPT_DSCR_HDR_LEN_2    7

/* Hid Atribute Mask */
#define HID_ATTR_MASK_VIRTUAL_CABLE        0x0001
#define HID_ATTR_MASK_NORMALLY_CONNECTABLE 0x0002
#define HID_ATTR_MASK_RECONN_INIT          0x0004
#define HID_ATTR_MASK_SDP_DISABLE          0x0008
#define HID_ATTR_MASK_BATTERY_POWER        0x0010
#define HID_ATTR_MASK_REMOTE_WAKE          0x0020
#define HID_ATTR_MASK_SUP_TOUT_AVLBL       0x0040
#define HID_ATTR_MASK_SSR_MAX_LATENCY      0x0080
#define HID_ATTR_MASK_SSR_MIN_TOUT         0x0100
#define HID_ATTR_MASK_SEC_REQUIRED         0x8000

using namespace tinyxml2;
struct enum_user_data
{
    const char* sn; //current section name
    const char* kn; //current key name
    const char* vn; //current value name
    int si, ki, vi;
    XMLDocument* xml;
    XMLElement* se;
    XMLElement* ke;
    XMLElement* ve;
};


static int type_str2int(const char* type);
static const char* type_int2str(int type);
static inline void create_ele_name(int index, char* element, int len);
static inline int validate_ele_name(const char* key);
static int parse_sections(const char* section_name, const XMLElement* section);
static void enum_config(void* user_data, const char* section, const char* key, const char* name,
                                          const char*  value, int bytes, int type);
static inline void bytes2hex(const char* data, int bytes, char* str)
{
    static const char* hex_table = "0123456789abcdef";
    for(int i = 0; i < bytes; i++)
    {
        *str = hex_table[(data[i] >> 4) & 0xf];
        ++str;
        *str = hex_table[data[i] & 0xf];
        ++str;
    }
    *str = 0;
}
static inline int hex2byte(char hex)
{
    if('0' <= hex && hex <= '9')
        return hex - '0';
    if('a' <= hex && hex <= 'z')
        return hex - 'a' + 0xa;
    if('A' <= hex && hex <= 'Z')
        return hex - 'A' + 0xa;
    return -1;
}
static inline int trim_bin_str_value(const char** str)
{
    while(**str == ' ' || **str == '\r' || **str == '\t' || **str == '\n')
        (*str)++;
    int len = 0;
    const char* s = *str;
    while(*s && *s != ' ' && *s != '\r' && *s != '\t' && *s != '\n')
    {
        len++;
        s++;
    }
    return len;
}
static inline bool hex2bytes(const char* str, int len, char* data)
{
    if(len % 2)
    {
        error("cannot convert odd len hex str: %s, len:%d to binary", str, len);
        return false;
    }
    for(int i = 0; i < len; i+= 2)
    {
        int d = hex2byte(str[i]);
        if(d < 0)
        {
            error("cannot convert hex: %s, len:%d to binary", str, len);
            return false;
        }
        *data = (char)(d << 4);
        d = hex2byte(str[i+1]);
        if(d < 0)
        {
            error("cannot convert hex: %s, len:%d to binary", str, len);
            return false;
        }
        *data++ |= (char)d;
    }
    return true;
}
static inline void reverse_bin(char *bin, int size)
{
    for(int i = 0; i < size /2; i++)
    {
        int b = bin[i];
        bin[i] = bin[size - i - 1];
        bin[size -i  - 1] = b;
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////
int btif_config_save_file(const char* file_name)
{
    debug("in file name:%s", file_name);
    XMLDocument xml;
    XMLElement* root = xml.NewElement(BLUEDROID_ROOT);
    xml.InsertFirstChild(root);
    int ret = FALSE;
    enum_user_data data;
    memset(&data, 0, sizeof(data));
    data.xml = &xml;
    if(btif_config_enum(enum_config, &data))
        ret = xml.SaveFile(file_name) == XML_SUCCESS;
    return ret;
}
int btif_config_load_file(const char* file_name)
{
    //if(access(file_name, 0) != 0)
    //    return XML_ERROR_FILE_NOT_FOUND;
    XMLDocument xml;
    int err = xml.LoadFile(file_name);
    const XMLElement* root = xml.RootElement();
    int ret = FALSE;
    if(err == XML_SUCCESS && root && strcmp(root->Name(), BLUEDROID_ROOT) == 0)
    {
        const XMLElement* section;
        for(section = root->FirstChildElement(); section; section = section->NextSiblingElement())
        {
            //debug("section tag:%s", section->Name());
            if(validate_ele_name(section->Name()))
            {
                const char* section_name = section->Attribute(BLUEDROID_NAME_TAG);
                if(section_name && *section_name)
                    if(parse_sections(section_name, section))
                        ret = TRUE;
            }
        }
    }
    return ret;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////
static int parse_sections(const char* section_name, const XMLElement* section)
{
    const XMLElement* key;
    //debug("in");
    for(key = section->FirstChildElement(); key; key = key->NextSiblingElement())
    {
        //debug("key tag:%s", key->Name());
        if(validate_ele_name(key->Name()))
        {
            const char* key_name = key->Attribute(BLUEDROID_NAME_TAG);
            //debug("key name:%s", key_name);
            if(key_name && *key_name)
            {
                const XMLElement* value;
                for(value = key->FirstChildElement(); value; value = value->NextSiblingElement())
                {
                    const char* value_name = value->Attribute(BLUEDROID_NAME_TAG);
                    const char* value_type = value->Attribute(BLUEDROID_VALUE_TYPE);
                    //debug("value ele name:%s, section name:%s, key name:%s, value name:%s, value type:%s",
                    //        value->Name(), section_name, key_name, value_name, value_type);
                    int type = BTIF_CFG_TYPE_INVALID;
                    if(value_type)
                    {
                        type = type_str2int((const char*)value_type);
                    }
                    if(value_name && *value_name && type != BTIF_CFG_TYPE_INVALID)
                    {
                        const char* value_str = value->GetText() ? value->GetText() : "";
                        //debug("value_name:%s, value_str:%s, value_type:%s, type:%x",
                        //       value_name, value_str, value_type, type);
                        if(type & BTIF_CFG_TYPE_STR)
                            btif_config_set_str(section_name, key_name, value_name, value_str);
                        else if(type & BTIF_CFG_TYPE_INT)
                        {
                            if(value_str && *value_str)
                            {
                                int v = atoi(value_str);
                                btif_config_set_int(section_name, key_name, value_name, v);
                            }
                        }
                        else if(type & BTIF_CFG_TYPE_BIN)
                        {
                            int len = trim_bin_str_value(&value_str);
                            if(len > 0 && len % 2 == 0)
                            {
                                char *bin = (char*)alloca(len / 2);
                                if(hex2bytes(value_str, len, bin))
                                    btif_config_set(section_name, key_name, value_name, bin, len/2, BTIF_CFG_TYPE_BIN);
                            }
                        }
                        else error("Unsupported value:%s, type:%s not loaded", value_name, value_type);
                    }
                }
            }
        }
    }
    //debug("out");
    return TRUE;
}
static inline XMLElement* add_ele(XMLDocument* xml, XMLElement* p, int index,
                                  const char* name_tag, const char* value_type = NULL)
{
    //debug("in, tag:%s", name_tag);
    char ele_name[128] = {0};
    create_ele_name(index, ele_name, sizeof(ele_name));
    XMLElement* ele = xml->NewElement(ele_name);
    //debug("ele name:%s, tag:%s, index:%d, value type:%s", ele_name, name_tag, index, value_type);
    ele->SetAttribute(BLUEDROID_NAME_TAG, name_tag);
    if(value_type && *value_type)
        ele->SetAttribute(BLUEDROID_VALUE_TYPE, value_type);
    p->InsertEndChild(ele);
    //debug("out, tag:%s", name_tag);
    return ele;
}
static void enum_config(void* user_data, const char* section_name, const char* key_name, const char* value_name,
                        const char*  value, int bytes, int type)
{
    enum_user_data& d = *(enum_user_data*)user_data;
    XMLElement * root_elem = d.xml->RootElement();
    //debug("in, key:%s, value:%s", key_name, value_name);
    //debug("section name:%s, key name:%s, value name:%s, value type:%s",
    //                      section_name, key_name, value_name, type_int2str(type));
    if(type & BTIF_CFG_TYPE_VOLATILE)
        return; //skip any volatile value
    if( (d.sn != section_name) && (root_elem != NULL) )
    {
        d.sn = section_name;
        d.se = add_ele(d.xml, root_elem, ++d.si, section_name);
        d.ki = 0;
    }
    if(d.kn != key_name)
    {
        d.kn = key_name;
        d.ke = add_ele(d.xml, d.se, ++d.ki, key_name);
        d.vi = 0;
    }
    if(d.vn != value_name)
    {
        if(type & BTIF_CFG_TYPE_STR)
        {
            d.vn = value_name;
            d.ve = add_ele(d.xml, d.ke, ++d.vi, value_name, type_int2str(type));
            d.ve->InsertFirstChild(d.xml->NewText(value));
        }
        else if(type & BTIF_CFG_TYPE_INT)
        {
            d.vn = value_name;
            d.ve = add_ele(d.xml, d.ke, ++d.vi, value_name, type_int2str(type));
            char value_str[64] = {0};
            snprintf(value_str, sizeof(value_str), "%d", *(int*)value);
            d.ve->InsertFirstChild(d.xml->NewText(value_str));
        }
        else if(type & BTIF_CFG_TYPE_BIN)
        {
            d.vn = value_name;
            d.ve = add_ele(d.xml, d.ke, ++d.vi, value_name, type_int2str(type));
            char* value_str = (char*)alloca(bytes*2 + 1);
            bytes2hex(value, bytes, value_str);
            d.ve->InsertFirstChild(d.xml->NewText(value_str));
        }
        else error("unsupported config value name:%s, type:%s not saved", d.vn, type_int2str(type));
    }
    //debug("out, key:%s, value:%s", key_name, value_name);
}

static int type_str2int(const char* type)
{
    if(type == NULL || *type == 0 || strcmp(type, "string") == 0)
        return  BTIF_CFG_TYPE_STR;
    if(strcmp(type, "int") == 0)
        return BTIF_CFG_TYPE_INT;
    if(strcmp(type, "binary") == 0)
        return BTIF_CFG_TYPE_BIN;
    error("unknown value type:%s", type);
    return BTIF_CFG_TYPE_INVALID;
}
static const char* type_int2str(int type)
{
    switch(type)
    {
        case BTIF_CFG_TYPE_INT:
            return "int";
        case BTIF_CFG_TYPE_BIN:
            return "binary";
        case BTIF_CFG_TYPE_STR:
            return "string";
        default:
            error("unknown type:%d", type);
            break;
    }
    return NULL;
}

static inline void create_ele_name(int index, char* element, int len)
{
    snprintf(element, len, "N%d", index);
}
static inline int validate_ele_name(const char* key)
{
    //must be 'N' followed with numbers
    if(key && *key == 'N' && *++key)
    {
        while(*key)
        {
            if(*key < '0' || *key > '9')
                return FALSE;
            ++key;
        }
        return TRUE;
    }
    return FALSE;
}
static int open_file_map(const char *pathname, const char**map, int* size)
{
    struct stat st;
    st.st_size = 0;
    int fd;
    //debug("in");
    if((fd = open(pathname, O_RDONLY)) >= 0)
    {
        //debug("fd:%d", fd);
        if(fstat(fd, &st) == 0 && st.st_size)
        {
            *size = st.st_size;
            *map = (const char*)mmap(NULL, *size, PROT_READ, MAP_SHARED, fd, 0);
            if(*map && *map != MAP_FAILED)
            {
                //debug("out map:%p, size:%d", *map, *size);
                return fd;
            }
        }
        close(fd);
    }
    //debug("out, failed");
    return -1;
}
static void close_file_map(int fd, const char* map, int size)
{
    munmap((void*)map, size);
    close(fd);
}
static int read_file_line(const char* map, int start_pos, int size, int* line_size)
{
    *line_size = 0;
    //debug("in, start pos:%d, size:%d", start_pos, size);
    int i;
    for(i = start_pos; i < size; i++)
    {
        if(map[i] == '\r' || map[i] == '\n')
            break;
         ++*line_size;
    }
    //debug("out, ret:%d, start pos:%d, size:%d, line_size:%d", i, start_pos, size, *line_size);
    return i + 1;
}
static const char* find_value_line(const char* map, int size, const char *key, int* value_size)
{
    int key_len = strlen(key);
    int i;
    for(i = 0; i < size; i++)
    {
        if(map[i] == *key)
        {
            if(i + key_len + 1 > size)
                return NULL;
            if(memcmp(map + i, key, key_len) == 0)
            {
                read_file_line(map, i + key_len + 1, size, value_size);
                if(*value_size)
                    return map + i + key_len + 1;
                break;
            }
        }
    }
    return NULL;
}
static int read_line_word(const char* line, int start_pos, int line_size, char* word, int *word_size, bool lower_case = false)
{
    int i;
    //skip space
    //debug("in, line start_pos:%d, line_size:%d", start_pos, line_size);
    for(i = start_pos; i < line_size; i++)
    {
        //debug("skip space loop, line[%d]:%c", i, line[i]);
        if(line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] !='\n')
            break;
    }
    *word_size = 0;
    for(; i < line_size; i++)
    {
        //debug("add word loop, line[%d]:%c", i, line[i]);
        if(line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] !='\n')
        {
            ++*word_size;
            if(lower_case && 'A' <= line[i] && line[i] <= 'Z')
                *word++ = 'a' - 'A' + line[i];
            else
                *word++ = line[i];
        }
        else break;
    }
    *word = 0;
    //debug("out, ret:%d, word:%s, word_size:%d, line start_pos:%d, line_size:%d",
    //            i, word, *word_size, start_pos, line_size);
    return i;
}
static int is_valid_bd_addr(const char* addr)
{
    int len = strlen(addr);
    //debug("addr: %s, len:%d", addr, len);
    return len == 17 && addr[2] == ':' && addr[5] == ':' && addr[14] == ':';
}
static int load_bluez_cfg_value(const char* adapter_path, const char* file_name)
{
    //debug("in");

    const char* map = NULL;
    int size = 0;
    int ret = FALSE;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", adapter_path, file_name);
    int fd = open_file_map(path, &map, &size);
    //debug("in, path:%s, fd:%d, size:%d", path, fd, size);
    if(fd < 0 || size == 0)
    {
        error("open_file_map fail, fd:%d, path:%s, size:%d", fd, path, size);
        //debug("out");
        if (fd >= 0)
            close(fd);
        return FALSE;
    }
    //get local bt device name from bluez config
    int line_size = 0;
    const char *value_line = find_value_line(map, size, "name", &line_size);
    if(value_line && line_size > 0)
    {
        char value[line_size + 1];
        memcpy(value, value_line, line_size);
        value[line_size] = 0;
        //debug("import local bt dev names:%s", value);
        btif_config_set_str("Local", "Adapter", "Name", value);
        ret = TRUE;
    }

    close_file_map(fd, map, size);
    //debug("out, ret:%d", ret);
    return ret;
}

int load_bluez_adapter_info(char* adapter_path, int size)
{
    struct dirent *dptr;
    DIR *dirp;
    int ret = FALSE;
    if((dirp = opendir(BLUEZ_PATH)) != NULL)
    {
        while((dptr = readdir(dirp)) != NULL)
        {
            //debug("readdir: %s",dptr->d_name);
            if(is_valid_bd_addr(dptr->d_name))
            {
                snprintf(adapter_path, size, "%s%s", BLUEZ_PATH, dptr->d_name);
                btif_config_set_str("Local", "Adapter", "Address", dptr->d_name);
                load_bluez_cfg_value(adapter_path, BLUEZ_CONFIG);
                ret = TRUE;
                break;
            }
        }
        closedir(dirp);
    }
    return ret;
}
static inline void upcase_addr(const char* laddr, char* uaddr, int size)
{
    int i;
    for(i = 0; i < size && laddr[i]; i++)
        uaddr[i] = ('a' <= laddr[i] && laddr[i] <= 'z') ?
                        laddr[i] - ('a' - 'A') : laddr[i];
    uaddr[i] = 0;
}

static int parse_hid_attribute(const char *str, int line_size, int len)
{
    if (len == 0 || line_size == 0 || str == NULL || (len%2))
        return 0;

    char hex_string[len + 1], hex_bytes[len/2];
    memcpy(hex_string, str - 1, len);
    hex_string[len] = 0;
    hex2bytes(hex_string, len, hex_bytes);
    if (len == 2)
        return hex_bytes[0];
    else if (len == 4)
        return hex_bytes[0] << 8 | hex_bytes[1];
    else return 0;
}

static int parse_bluez_hid_sdp_records(const char* adapter_path, const char* bd_addr)
{
    //debug("in");
    char addr[32];
    char pattern_to_search[50];
    upcase_addr(bd_addr, addr, sizeof(addr));

    const char* map = NULL;
    int size = 0;
    int ret = FALSE;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", adapter_path, BLUEZ_SDP);
    int fd = open_file_map(path, &map, &size);
    //debug("in, path:%s, addr:%s, fd:%d, size:%d", path, addr, fd, size);
    if(fd < 0 || size == 0)
    {
        error("open_file_map fail, fd:%d, path:%s, size:%d", fd, path, size);
        //debug("out");
        return FALSE;
    }
    int line_size = 0;
    snprintf(pattern_to_search, sizeof(pattern_to_search), "%s#00010000", addr);
    const char *value_line = find_value_line(map, size, pattern_to_search, &line_size);
    int dev_sub_class = 0;
    int app_id = 0;
    int countrycode = 0;
    int product = 0;
    int vendor = 0;
    int product_ver = 0;
    int attr_mask = 0;
    int ssr_max_lat = 0;
    int ssr_min_timeout = 0;
    int rep_desc_len = 0;
    if(value_line && line_size)
    {
        char hid_sdp[line_size + 2];
        memcpy(hid_sdp, value_line - 1, line_size);
        hid_sdp[line_size + 1] = 0;
        //debug("addr:%s, hid_sdp:%s", bd_addr, hid_sdp);
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_SUB_CLASS, &line_size);
        dev_sub_class = parse_hid_attribute(value_line, line_size, 2);
        if(dev_sub_class)
        {
            if ((dev_sub_class & 0x80) == 0x80)
                app_id = HID_APP_ID_MOUSE;
            else
                app_id = HID_APP_ID_KYB;
            //debug("dev_sub_class:%d", dev_sub_class);
        }
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_COUNTRY_CODE, &line_size);
        countrycode = parse_hid_attribute(value_line, line_size, 2);
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_VIRTUAL_CABLE, &line_size);
        if(parse_hid_attribute(value_line, line_size, 2))
        {
            attr_mask |= HID_ATTR_MASK_VIRTUAL_CABLE;
            //debug("attr_mask after Virtual Unplug:%04x", attr_mask);
        }
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_RECON_INNITIATE, &line_size);
        if(parse_hid_attribute(value_line, line_size, 2))
        {
            attr_mask |= HID_ATTR_MASK_RECONN_INIT;
            //debug("attr_mask after Reconnect Initiate:%04x", attr_mask);
        }
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_REP_DSC_1, &line_size);
        if(value_line && line_size)
        {
            char rep_desc[line_size + 1], rd[line_size/2 + 1];
            char rep_dsc_len[5], rd_len[2];
            memcpy(rep_dsc_len, value_line - 1, 4);
            rep_dsc_len[4] = 0;
            hex2bytes(rep_dsc_len, 4, rd_len);
            rep_desc_len = (rd_len[0] << 8 | rd_len[1]) - (HID_RPT_DSCR_HDR_LEN_1 - 2);
            //debug("rep_desc_len:%d", rep_desc_len);
            memcpy(rep_desc, value_line - 1 + (HID_RPT_DSCR_HDR_LEN_1 * 2), rep_desc_len * 2);
            rep_desc[rep_desc_len * 2] = 0;
            hex2bytes(rep_desc, rep_desc_len* 2, rd);
            if (rep_desc_len)
            {
                //debug("rep_desc:%s", rep_desc);
                btif_config_set("Remote", bd_addr, "HidDescriptor", rd, rep_desc_len,
                        BTIF_CFG_TYPE_BIN);
            }
        }
        else
        {
            value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_REP_DSC_2, &line_size);
            if(value_line && line_size)
            {
                char rep_dsc_len[3], rd_len[1];
                memcpy(rep_dsc_len, value_line - 1, 2);
                rep_dsc_len[2] = 0;
                hex2bytes(rep_dsc_len, 2, rd_len);
                rep_desc_len = rd_len[0] - (HID_RPT_DSCR_HDR_LEN_2 - 1);
                //debug("rep_desc_len:%d", rep_desc_len);
                char rep_desc[(rep_desc_len * 2) + 1], rd[rep_desc_len + 1];
                memcpy(rep_desc, value_line - 1 + (HID_RPT_DSCR_HDR_LEN_2 * 2), rep_desc_len * 2);
                rep_desc[rep_desc_len * 2] = 0;
                hex2bytes(rep_desc, rep_desc_len * 2, rd);
                if (rep_desc_len)
                {
                    //debug("rep_desc:%s", rep_desc);
                    btif_config_set("Remote", bd_addr, "HidDescriptor", rd, rep_desc_len,
                            BTIF_CFG_TYPE_BIN);
                }
            }
        }
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_SDP_DISABLE, &line_size);
        if(parse_hid_attribute(value_line, line_size, 2))
        {
            attr_mask |= HID_ATTR_MASK_SDP_DISABLE;
            //debug("attr_mask after SDP Disable:%04x", attr_mask);
        }
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_BAT_POWER, &line_size);
        if(parse_hid_attribute(value_line, line_size, 2))
        {
            attr_mask |= HID_ATTR_MASK_BATTERY_POWER;
            //debug("attr_mask after Battery Powered:%04x", attr_mask);
        }
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_REM_WAKE, &line_size);
        if(parse_hid_attribute(value_line, line_size, 2))
        {
            attr_mask |= HID_ATTR_MASK_REMOTE_WAKE;
            //debug("attr_mask after Remote Wake:%04x", attr_mask);
        }
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_NORM_CONN, &line_size);
        if(parse_hid_attribute(value_line, line_size, 2))
        {
            attr_mask |= HID_ATTR_MASK_NORMALLY_CONNECTABLE;
            //debug("attr_mask after Normally Conenctable:%04x", attr_mask);
        }
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_SUP_TIME, &line_size);
        if(value_line && line_size)
            attr_mask |= HID_ATTR_MASK_SUP_TOUT_AVLBL;
        //debug("attr_mask after Supervision Timeout:%04x", attr_mask);
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_SSR_MAX_LAT, &line_size);
        ssr_max_lat = parse_hid_attribute(value_line, line_size, 4);
        if(!ssr_max_lat)
            ssr_max_lat = HID_SSR_PARAM_INVALID;
        value_line = find_value_line(hid_sdp, strlen(hid_sdp), HID_SSR_MIN_TIM, &line_size);
        ssr_min_timeout = parse_hid_attribute(value_line, line_size, 4);
        if(!ssr_min_timeout)
            ssr_min_timeout = HID_SSR_PARAM_INVALID;
        snprintf(pattern_to_search, sizeof(pattern_to_search), "%s#00010001", addr);
        value_line = find_value_line(map, size, pattern_to_search, &line_size);
        if(value_line && line_size)
        {
            char did_sdp[line_size + 2];
            memcpy(did_sdp, value_line - 1, line_size + 1);
            did_sdp[line_size + 1] = 0;
            //debug("addr:%s, did_sdp:%s", bd_addr, did_sdp);
            value_line = find_value_line(did_sdp, strlen(did_sdp), HID_VENDOR_ID, &line_size);
            vendor = parse_hid_attribute(value_line, line_size, 4);
            value_line = find_value_line(did_sdp, strlen(did_sdp), HID_PRODUCT_ID, &line_size);
            product = parse_hid_attribute(value_line, line_size, 4);
            value_line = find_value_line(did_sdp, strlen(did_sdp), HID_PRODUCT_VERSION, &line_size);
            product_ver = parse_hid_attribute(value_line, line_size, 4);
         }
    }
    btif_config_set_int("Remote", bd_addr, "HidAttrMask", attr_mask);
    btif_config_set_int("Remote", bd_addr, "HidSubClass", dev_sub_class);
    btif_config_set_int("Remote", bd_addr, "HidAppId", app_id);
    btif_config_set_int("Remote", bd_addr, "HidVendorId", vendor);
    btif_config_set_int("Remote", bd_addr, "HidProductId", product);
    btif_config_set_int("Remote", bd_addr, "HidVersion", product_ver);
    btif_config_set_int("Remote", bd_addr, "HidCountryCode", countrycode);
    btif_config_set_int("Remote", bd_addr, "HidSSRMinTimeout", ssr_min_timeout);
    btif_config_set_int("Remote", bd_addr, "HidSSRMaxLatency", ssr_max_lat);
    //debug("HidSubClass: %02x, app_id = %d, vendor = %04x, product = %04x, product_ver = %04x"
    //    "countrycode = %02x, ssr_min_timeout = %04x, ssr_max_lat = %04x",
    //    HidSubClass, app_id, vendor, product, product_ver, countrycode, ssr_min_timeout,
    //    ssr_max_lat);
    close_file_map(fd, map, size);
    ret = TRUE;
    //debug("out, ret:%d", ret);
    return ret;
}

static int load_bluez_dev_value(const char* adapter_path, const char* bd_addr,
                                const char* file_name, const char* cfg_value_name, int type)
{
    //debug("in");
    char addr[32];
    upcase_addr(bd_addr, addr, sizeof(addr));

    const char* map = NULL;
    int size = 0;
    int ret = FALSE;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", adapter_path, file_name);
    int fd = open_file_map(path, &map, &size);
    //debug("in, path:%s, addr:%s, fd:%d, size:%d", path, addr, fd, size);
    if(fd < 0 || size == 0)
    {
        error("open_file_map fail, fd:%d, path:%s, size:%d", fd, path, size);
        //debug("out");
        if (fd >= 0)
            close(fd);
        return FALSE;
    }
    int line_size = 0;
    const char *value_line = find_value_line(map, size, addr, &line_size);
    if(value_line && line_size)
    {
        char line[line_size + 1];
        memcpy(line, value_line, line_size);
        line[line_size] = 0;
        //debug("addr:%s, Names:%s", bd_addr, line);
        if(type == BTIF_CFG_TYPE_STR)
            btif_config_set_str("Remote", bd_addr, cfg_value_name, line);
        else if(type == BTIF_CFG_TYPE_INT)
        {
            int v = strtol(line, NULL, 16);
            //parse sdp record in case remote device is hid
            if(strcmp(file_name, BLUEZ_CLASSES) == 0)
            {
                switch((v & 0x1f00) >> 8)
                {
                    case 0x5: //hid device
                        info("parsing sdp for hid device %s", bd_addr);
                        parse_bluez_hid_sdp_records(adapter_path, bd_addr);
                        break;
                }
            }
            btif_config_set_int("Remote", bd_addr, cfg_value_name, v);
        }
        ret = TRUE;
    }
    close_file_map(fd, map, size);
    //debug("out, ret:%d", ret);
    return ret;
}
static inline int bz2bd_linkkeytype(int type)
{
#if 1
    return type;
#else
    int table[5] = {0, 0, 0, 0, 0};
    if(0 <= type && type < (int)(sizeof(table)/sizeof(int)))
        return table[type];
    return 0;
#endif
}
int load_bluez_linkkeys(const char* adapter_path)
{
    const char* map = NULL;
    int size = 0;
    int ret = FALSE;
    char path[256];
    //debug("in");
    snprintf(path, sizeof(path), "%s/%s", adapter_path, BLUEZ_LINKKEY);
    int fd = open_file_map(path, &map, &size);
    if(fd < 0 || size == 0)
    {
        error("open_file_map fail, fd:%d, path:%s, size:%d", fd, path, size);
        //debug("out");
        if (fd >= 0)
            close(fd);
        return FALSE;
    }
    int pos = 0;
    //debug("path:%s, size:%d", path, size);
    while(pos < size)
    {
        int line_size = 0;
        int next_pos = read_file_line(map, pos, size, &line_size);
        //debug("pos:%d, next_pos:%d, size:%d, line_size:%d", pos, next_pos, size, line_size);
        if(line_size)
        {
            const char* line = map + pos;
            char addr[line_size + 1];
            int word_pos = 0;
            int addr_size = 0;
            word_pos = read_line_word(line, word_pos, line_size, addr, &addr_size, true);
            //debug("read_line_word addr:%s, addr_size:%d", addr, addr_size);
            if(*addr)
            {
                char value[line_size + 1];
                int value_size = 0;
                //read link key
                word_pos = read_line_word(line, word_pos, line_size, value, &value_size);
                //debug("read_line_word linkkey:%s, size:%d", value, value_size);
                if(*value)
                {
                    int linkkey_size = value_size / 2;
                    char linkkey[linkkey_size];
                    if(hex2bytes(value, value_size, linkkey))
                    { //read link key type
                        //bluez save the linkkey in reversed order
                        reverse_bin(linkkey, linkkey_size);
                        word_pos = read_line_word(line, word_pos,
                                                    line_size, value, &value_size);
                        if(*value)
                        {
                            if(load_bluez_dev_value(adapter_path, addr,
                                                BLUEZ_CLASSES, "DevClass", BTIF_CFG_TYPE_INT) ||
                               load_bluez_dev_value(adapter_path, addr,
                                                BLUEZ_NAMES, "Name", BTIF_CFG_TYPE_STR) ||
                               load_bluez_dev_value(adapter_path, addr,
                                                BLUEZ_TYPES, "DevType", BTIF_CFG_TYPE_INT) ||
                               load_bluez_dev_value(adapter_path, addr,
                                                BLUEZ_PROFILES, "Service", BTIF_CFG_TYPE_STR))
                            {
                                load_bluez_dev_value(adapter_path, addr,
                                                BLUEZ_ALIASES, "Aliase", BTIF_CFG_TYPE_STR);
                                int key_type = bz2bd_linkkeytype(atoi(value));

                                //read pin len
                                word_pos = read_line_word(line, word_pos, line_size, value, &value_size);
                                if(*value)
                                {
                                    int pin_len = atoi(value);
                                    ret = TRUE;
                                    btif_config_set("Remote", addr, "LinkKey", linkkey,
                                                                    linkkey_size, BTIF_CFG_TYPE_BIN);
                                    //dump_bin("import bluez linkkey", linkkey, linkkey_size);
                                    btif_config_set_int("Remote", addr, "LinkKeyType", key_type);
                                    btif_config_set_int("Remote", addr, "PinLength", pin_len);
                                }
                            }
                        }
                    }
                }
            }
        }
        //debug("pos:%d, next_pos:%d, size:%d, line_size:%d", pos, next_pos, size, line_size);
        pos = next_pos;
    }
    close_file_map(fd, map, size);
    //debug("out, ret:%d", ret);
    return ret;
}

