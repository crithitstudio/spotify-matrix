// THE OBSERVER - content loading. Everything authored lives in content/*.json.
#include "game.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

static vec3 jvec3(const json& j, vec3 def = {}) {
    if (!j.is_array() || j.size() < 3) return def;
    return {j[0], j[1], j[2]};
}
static vec4 jvec4(const json& j, vec4 def = {1, 1, 1, 1}) {
    if (!j.is_array() || j.size() < 3) return def;
    return {j[0], j[1], j[2], j.size() > 3 ? (float)j[3] : 1.0f};
}

static bool parseJsonFile(const std::string& path, json& out, std::string& err) {
    std::string txt;
    if (!readFileText(path, txt)) {
        err = "missing " + path;
        return false;
    }
    out = json::parse(txt, nullptr, false);
    if (out.is_discarded()) {
        err = "bad json in " + path;
        return false;
    }
    return true;
}

bool loadContent(GameContent& c, const std::string& dir, std::string& err) {
    json b;
    if (!parseJsonFile(dir + "/building.json", b, err)) return false;

    json texj = b.value("textures", json::object());
    for (auto& [name, path] : texj.items()) c.textures[name] = path.get<std::string>();
    json sndj = b.value("sounds", json::object());
    for (auto& [name, path] : sndj.items()) c.sounds[name] = path.get<std::string>();

    for (auto& r : b.value("rooms", json::array())) {
        RoomSpec rs;
        rs.id = r.value("id", "");
        rs.name = r.value("name", rs.id);
        rs.mn = jvec3(r["min"]);
        rs.mx = jvec3(r["max"]);
        rs.wallTex = r.value("wall", "plaster");
        rs.floorTex = r.value("floor", "concrete_floor");
        rs.ceilTex = r.value("ceil", "ceiling_tile");
        rs.wallUv = r.value("wallUv", 0.5f);
        rs.floorUv = r.value("floorUv", 0.5f);
        rs.zone = r.value("zone", 0);
        for (auto& t : r.value("tags", json::array())) rs.tags.push_back(t.get<std::string>());
        c.rooms.push_back(rs);
    }
    for (auto& d : b.value("doors", json::array())) {
        DoorSpec ds;
        ds.id = d.value("id", "");
        ds.label = d.value("label", "");
        ds.roomA = d.value("a", "");
        ds.roomB = d.value("b", "");
        ds.pos = jvec3(d["pos"]);
        ds.axis = d.value("axis", 0);
        ds.locked = d.value("locked", false);
        ds.open = d.value("open", false);
        ds.keyId = d.value("key", "");
        ds.tex = d.value("tex", "door_wood");
        ds.isThreshold = d.value("threshold", false);
        ds.thresholdTarget = d.value("to", "");
        c.doors.push_back(ds);
    }
    for (auto& p : b.value("props", json::array())) {
        PropSpec ps;
        ps.id = p.value("id", "");
        std::string t = p.value("type", "box");
        ps.type = t == "quad" ? PropType::Quad : (t == "billboard" ? PropType::Billboard : PropType::Box);
        ps.room = p.value("room", "");
        ps.pos = jvec3(p["pos"]);
        ps.size = jvec3(p.value("size", json::array({1, 1, 1})), {1, 1, 1});
        ps.yaw = radians(p.value("yaw", 0.0f));
        ps.tex = p.value("tex", "");
        ps.tint = jvec4(p.value("tint", json::array({1, 1, 1, 1})));
        ps.emissive = p.value("emissive", 0.0f);
        ps.collide = p.value("collide", true);
        ps.mirrorOnly = p.value("mirrorOnly", false);
        ps.hidden = p.value("hidden", false);
        ps.interact = p.value("interact", "");
        ps.flat = p.value("flat", false);
        ps.uvx = p.value("uvx", 1.0f);
        ps.uvy = p.value("uvy", 1.0f);
        c.props.push_back(ps);
    }
    for (auto& l : b.value("lights", json::array())) {
        LightSpec ls;
        ls.id = l.value("id", "");
        ls.room = l.value("room", "");
        ls.pos = jvec3(l["pos"]);
        ls.color = jvec3(l.value("color", json::array({1.0, 0.92, 0.78})), {1.0f, 0.92f, 0.78f});
        ls.intensity = l.value("intensity", 1.0f);
        ls.radius = l.value("radius", 6.0f);
        ls.on = l.value("on", true);
        ls.flicker = l.value("flicker", 0.0f);
        c.lights.push_back(ls);
    }
    for (auto& cm : b.value("cams", json::array())) {
        CamSpec cs;
        cs.id = cm.value("id", "");
        cs.label = cm.value("label", cs.id);
        cs.room = cm.value("room", "");
        cs.pos = jvec3(cm["pos"]);
        cs.yaw = radians(cm.value("yaw", 0.0f));
        cs.pitch = radians(cm.value("pitch", -20.0f));
        c.cams.push_back(cs);
    }
    for (auto& m : b.value("mirrors", json::array())) {
        MirrorSpec ms;
        ms.id = m.value("id", "");
        ms.room = m.value("room", "");
        ms.pos = jvec3(m["pos"]);
        ms.yaw = radians(m.value("yaw", 0.0f));
        ms.size = {m.value("w", 0.7f), m.value("h", 1.1f)};
        c.mirrors.push_back(ms);
    }
    c.playerStartRoom = b.value("playerRoom", "");
    c.playerStart = jvec3(b.value("playerStart", json::array({0, 0, 0})));
    c.playerStartYaw = radians(b.value("playerYaw", 0.0f));

    // ---- anomaly event templates
    json a;
    if (!parseJsonFile(dir + "/anomalies.json", a, err)) return false;
    for (auto& e : a.value("events", json::array())) {
        EventSpec es;
        es.id = e.value("id", "");
        es.channel = e.value("channel", "SOUND");
        es.minAct = e.value("minAct", 1);
        es.maxAct = e.value("maxAct", 5);
        es.cooldown = e.value("cooldown", 90.0f);
        es.weight = e.value("weight", 1.0f);
        es.once = e.value("once", false);
        es.needUnobservedRoom = e.value("unobserved", false);
        es.effect = e.value("effect", "script");
        es.room = e.value("room", "");
        es.target = e.value("target", "");
        es.param = e.value("param", "");
        es.value = e.value("value", 0.0f);
        c.events.push_back(es);
    }

    // ---- acts
    json ac;
    if (!parseJsonFile(dir + "/acts.json", ac, err)) return false;
    for (auto& av : ac.value("acts", json::array())) {
        ActSpec as;
        as.index = av.value("index", (int)c.acts.size() + 1);
        as.title = av.value("title", "");
        as.clock = av.value("clock", "23:00");
        as.ambientScale = av.value("ambient", 1.0f);
        as.directorInterval = av.value("interval", 45.0f);
        as.witnessCap = av.value("witnessCap", 0);
        for (auto& bv : av.value("beats", json::array())) {
            BeatSpec bs;
            bs.id = bv.value("id", "");
            bs.trigger = bv.value("trigger", "time");
            bs.arg = bv.value("arg", "");
            bs.atTime = bv.value("at", 0.0f);
            for (auto& act2 : bv.value("do", json::array())) bs.actions.push_back(act2.get<std::string>());
            as.beats.push_back(bs);
        }
        c.acts.push_back(as);
    }
    if (c.acts.empty()) {
        err = "no acts";
        return false;
    }

    // ---- documents
    json d;
    if (!parseJsonFile(dir + "/documents.json", d, err)) return false;
    for (auto& dv : d.value("documents", json::array())) {
        DocumentSpec ds;
        ds.id = dv.value("id", "");
        ds.title = dv.value("title", "");
        ds.type = dv.value("type", "note");
        for (auto& l : dv.value("lines", json::array())) ds.lines.push_back(l.get<std::string>());
        c.documents.push_back(ds);
    }

    // ---- strings
    json s;
    if (parseJsonFile(dir + "/strings.json", s, err)) {
        for (auto& [k, v] : s.items()) c.strings[k] = v.get<std::string>();
    }
    err.clear();
    return true;
}
