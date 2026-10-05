# netkit.bi -- netkit for Onyx BASIC (#import netkit): made by tools/kitbi/kitbi.py from netkit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit netkit 21
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
ftpfs_forget 5 i s ftpfs_forget
ftpfs_format_site 6 i ppi ftpfs_format_site
ftpfs_load_sites 7 i pi ftpfs_load_sites
ftpfs_login 8 i sssi ftpfs_login
ftpfs_login_site 9 i pi ftpfs_login_site
ftpfs_obf_hex 10 v spi ftpfs_obf_hex
ftpfs_parse_site 11 i sp ftpfs_parse_site
ftpfs_text_fix 12 i pi ftpfs_text_fix
ftpfs_unobf_hex 13 v spi ftpfs_unobf_hex
http_get 18 i spip http_get
http_post 19 i ssipip http_post
http_request 20 i ssssipip http_request
