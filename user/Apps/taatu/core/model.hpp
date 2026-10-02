//
// model.hpp -- the room/world state the client keeps: avatars (paper-doll), chat log.
// Parsed from the Socket.IO payloads (json.hpp). Fixed-size arrays, no STL, so it links on
// Onyx and the PC alike.
//
#ifndef TAATU_MODEL_HPP
#define TAATU_MODEL_HPP
#include "json.hpp"
#include <string.h>

namespace taatu {

enum { MAX_AVATARS = 128, MAX_CHAT = 64, N_LAYERS = 7 };

// Clothing layers, draw order bottom..top (see DESIGN.md §4.3).
enum Layer { L_BOTTOM = 0, L_TOP, L_SHOES, L_BEARD, L_HAIR, L_GLASSES, L_HAT };
static const char *const LAYER_KEY[N_LAYERS] = { "bottom", "top", "shoes", "beard", "hair", "glasses", "hat" };

struct ClothingItem
{
    char sprite_path[160];
    char color_hex[8];       // "" if none
    int  color_editable;
    int  hide_base_sprite, hide_bottom;
    bool present;
    ClothingItem () { clear (); }
    void clear () { sprite_path[0] = 0; color_hex[0] = 0; color_editable = 0; hide_base_sprite = hide_bottom = 0; present = false; }
};

struct Avatar
{
    long long user_id;
    char pseudo[40];
    int  gender;             // 0 male, 1 female
    char user_status[20];
    int  x, z, direction, head_pointing;
    int  is_dancing, is_sitting, in_bed_slot, is_absent, is_in_game, invisible;
    int  typing;
    char skin_tone_hex[8];
    char eye_sprite_path[160];
    ClothingItem cloth[N_LAYERS];
    bool used;

    Avatar () : user_id (0), gender (0), x (0), z (0), direction (0), head_pointing (0),
        is_dancing (0), is_sitting (0), in_bed_slot (0), is_absent (0), is_in_game (0),
        invisible (0), typing (0), used (false)
    { pseudo[0] = user_status[0] = skin_tone_hex[0] = eye_sprite_path[0] = 0; }
};

struct ChatMsg
{
    long long user_id;
    char pseudo[40];
    char message[256];
    char color[8];
};

inline void cpy (char *d, int cap, const char *s) { int i = 0; if (!s) { d[0] = 0; return; } while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }

class World
{
public:
    long long self_id;
    int room_id;
    bool is_appart;
    Avatar av[MAX_AVATARS];
    int n_av;
    ChatMsg chat[MAX_CHAT];
    int chat_head, chat_count;      // ring buffer

    World () : self_id (0), room_id (0), is_appart (false), n_av (0), chat_head (0), chat_count (0) {}

    void clear_room () { n_av = 0; }

    Avatar *find (long long id) { for (int i = 0; i < n_av; i++) if (av[i].used && av[i].user_id == id) return &av[i]; return 0; }
    Avatar *find_or_add (long long id)
    {
        Avatar *a = find (id); if (a) return a;
        if (n_av < MAX_AVATARS) { Avatar *b = &av[n_av++]; *b = Avatar (); b->user_id = id; b->used = true; return b; }
        return 0;
    }
    void remove (long long id)
    {
        for (int i = 0; i < n_av; i++) if (av[i].used && av[i].user_id == id) { av[i] = av[--n_av]; return; }
    }

    // ---- parsing from Socket.IO payloads --------------------------------------
    static void parse_clothing (Avatar *a, const json::Value &eq)
    {
        for (int i = 0; i < N_LAYERS; i++)
        {
            a->cloth[i].clear ();
            const json::Value &it = eq[LAYER_KEY[i]];
            if (!it.isObj ()) continue;
            cpy (a->cloth[i].sprite_path, sizeof a->cloth[i].sprite_path, it["sprite_path"].asStr (""));
            cpy (a->cloth[i].color_hex, sizeof a->cloth[i].color_hex, it["color_hex"].asStr (""));
            a->cloth[i].color_editable = it["color_editable"].asInt (0);
            a->cloth[i].hide_base_sprite = it["hide_base_sprite"].asBool (false) ? 1 : 0;
            a->cloth[i].hide_bottom = it["hide_bottom"].asBool (false) ? 1 : 0;
            a->cloth[i].present = a->cloth[i].sprite_path[0] != 0;
        }
    }
    void apply_full (const json::Value &p)        // a room-players[] entry (full avatar)
    {
        Avatar *a = find_or_add (p["user_id"].asLong (0)); if (!a) return;
        cpy (a->pseudo, sizeof a->pseudo, p["pseudo"].asStr (""));
        a->gender = strcmp (p["gender"].asStr ("male"), "female") == 0 ? 1 : 0;
        cpy (a->user_status, sizeof a->user_status, p["user_status"].asStr (""));
        a->x = p["position_x"].asInt (0); a->z = p["position_z"].asInt (0);
        a->direction = p["direction"].asInt (0); a->head_pointing = p["head_pointing"].asInt (0);
        a->is_dancing = p["is_dancing"].asBool (false); a->is_sitting = p["is_sitting"].asBool (false);
        a->in_bed_slot = p["in_bed_slot"].asInt (0); a->is_absent = p["is_absent"].asBool (false);
        a->is_in_game = p["is_in_game"].asBool (false); a->invisible = p["invisible"].asBool (false);
        cpy (a->skin_tone_hex, sizeof a->skin_tone_hex, p["skin_tone_hex"].asStr (""));
        cpy (a->eye_sprite_path, sizeof a->eye_sprite_path, p["eye_sprite_path"].asStr (""));
        parse_clothing (a, p["equipped_clothing"]);
    }
    void apply_tick (const json::Value &u)        // a room-tick updates[] entry (state only)
    {
        Avatar *a = find_or_add (u["user_id"].asLong (0)); if (!a) return;
        if (a->pseudo[0] == 0) cpy (a->pseudo, sizeof a->pseudo, u["pseudo"].asStr (""));
        a->x = u["position_x"].asInt (a->x); a->z = u["position_z"].asInt (a->z);
        a->direction = u["direction"].asInt (a->direction); a->head_pointing = u["head_pointing"].asInt (a->head_pointing);
        a->is_dancing = u["is_dancing"].asBool (a->is_dancing); a->is_sitting = u["is_sitting"].asBool (a->is_sitting);
        a->in_bed_slot = u["in_bed_slot"].asInt (a->in_bed_slot); a->is_absent = u["is_absent"].asBool (a->is_absent);
        a->is_in_game = u["is_in_game"].asBool (a->is_in_game); a->invisible = u["invisible"].asBool (a->invisible);
    }
    void push_chat (const json::Value &m)
    {
        int idx = (chat_head + chat_count) % MAX_CHAT;
        ChatMsg &slot = chat[idx];
        slot.user_id = m["user_id"].asLong (0);
        cpy (slot.pseudo, sizeof slot.pseudo, m["pseudo"].asStr (""));
        cpy (slot.message, sizeof slot.message, m["message"].asStr (""));
        cpy (slot.color, sizeof slot.color, m["textColor"].asStr ("#000000"));
        if (chat_count < MAX_CHAT) chat_count++; else chat_head = (chat_head + 1) % MAX_CHAT;
    }
    const ChatMsg &chat_at (int i) const { return chat[(chat_head + i) % MAX_CHAT]; }
};

} // namespace taatu
#endif
