# inputkit.bi -- inputkit for Onyx BASIC (#import inputkit): made by tools/kitbi/kitbi.py from inputkit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit inputkit 12
struct event 24 in_event
field action 0 i
field kind 4 i
field source 8 i
field code 12 i
field mods 16 i
field player 20 i
struct state 28 in_state
field held 0 u
field lx 4 i
field ly 8 i
field rx 12 i
field ry 16 i
field connected 20 i
field source 24 i
action_name 0 s i in_action_name action
app_owns_pad 1 i s in_app_owns_pad app
binding 2 i iipi in_binding action,source,out,cap
claim 3 v i in_claim on
claimed 4 i - in_claimed
key_action 5 i ii in_key_action key,mods
key_event 6 i iip in_key_event key,mods,out
load 7 i - in_load
load_text 8 i s in_load_text text
pad_mask 9 u i in_pad_mask action
poll 10 i pi in_poll out,max
state 11 i ip in_state player,out
