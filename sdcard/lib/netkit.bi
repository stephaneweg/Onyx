# netkit.bi -- netkit for Onyx BASIC (#import netkit): made by tools/kitbi/kitbi.py from netkit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit netkit 28
struct ftpfs_site 396 ftpfs_site
field host 0 a 128
field user 128 a 64
field pass 192 a 64
field port 256 a 8
field folder 264 a 128
field tls 392 i
struct http_response 32 http_response
field status 0 i
field ok 4 i
field body 8 l
field body_len 16 i
field total_len 20 i
field truncated 24 i
struct wifi_known 144 wifi_known
field ssid 0 a 33
field psk 33 a 64
field keymgmt 97 a 24
field proto 121 a 16
field priority 140 i
ftpfs_forget 5 i s ftpfs_forget host
ftpfs_format_site 6 i ppi ftpfs_format_site s,out,cap
ftpfs_load_sites 7 i pi ftpfs_load_sites sites,max
ftpfs_login 8 i sssi ftpfs_login host,user,pass,remember
ftpfs_login_site 9 i pi ftpfs_login_site s,remember
ftpfs_obf_hex 10 v spi ftpfs_obf_hex in,out,cap
ftpfs_parse_site 11 i sp ftpfs_parse_site line,s
ftpfs_text_fix 12 i pi ftpfs_text_fix b,n
ftpfs_unobf_hex 13 v spi ftpfs_unobf_hex in,out,cap
http_get 18 i spip http_get url,buf,cap,r
http_post 19 i ssipip http_post url,body,len,buf,cap,r
http_request 20 i ssssipip http_request method,url,xheaders,body,body_len,buf,cap,resp
wifi_forget 21 i s wifi_forget ssid
wifi_is_known 22 i s wifi_is_known ssid
wifi_join 23 i sis wifi_join ssid,security,psk
wifi_join_as 24 i ssss wifi_join_as ssid,psk,keymgmt,proto
wifi_known_load 25 i pipi wifi_known_load out,max,country,ccap
wifi_known_save 26 i pis wifi_known_save k,n,country
wifi_set_country 27 i s wifi_set_country country
