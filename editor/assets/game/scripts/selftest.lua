-- The game's own check of the level, run by `wiwa_editor --game-smoke`: the self test of the main menu scene (menu_selftest.lua) raises the
-- flag prefs "smoke" when it loads the level, and this script then walks the game through its whole loop, logging "GAMESMOKE PASS" or
-- "GAMESMOKE FAIL: <why>". Without the flag it does nothing.
local Game = require("assets/game/scripts/lib/game.lua")
local Options = require("assets/game/scripts/lib/options.lua")

local Selftest = Script:extend{}

local function check(condition, message)
  if not condition then
    error(message, 0)
  end
end

local function near(a, b, distance)
  return vec3(a.x - b.x, 0, a.z - b.z):length() <= distance
end

local function copy(p)
  return vec3(p.x, p.y, p.z)
end

local function door_of(group)
  for _, door in ipairs(Game.doors) do
    if door.group == group then
      return door
    end
  end
  error("no door for the group " .. group, 0)
end

local steps = {}

local function step(name, fn)
  steps[#steps + 1] = { name = name, fn = fn }
end

step("arrival", function(me, dt, s)
  if not Options.ready then -- the panels load in the background
    check(me.t < 20, "the level UI did not finish loading")
    return false
  end
  check(Game.state == "playing", "the level should start playing, it is " .. Game.state)
  check(Options.ready and Game.ui_missing == 0, "the level UI is not set up or looks for widgets it does not have (" .. Game.ui_missing .. ")")
  check(Game.hero == "wizard", "the hero chosen in the main menu should arrive in the level (it is " .. Game.hero .. ")")
  Game.select_hero("warrior")
  Game.start()
  return true
end)

step("start", function(me, dt, s)
  if me.t > 0.5 then
    check(Game.player_script.model.name == "Warrior model", "the warrior model is not shown")
    check(Game.state == "playing", "start did not lead to playing")
    check(Game.hero == "warrior", "the selected hero should be the warrior")
    Game.god = true
    return true
  end
end)

step("walk", function(me, dt, s)
  if me.t == 0 then
    s.z = Game.player_position().z
    Game.fake_move = vec2(0, 1)
  end
  if me.t > 1.5 then
    Game.fake_move = nil
    check(s.z - Game.player_position().z > 1.0, "the warrior did not walk forward")
    return true
  end
end)

step("facing", function(me, dt, s)
  -- the helper: a model turned by yaw_of(d) faces d (the models face +z)
  for _, d in ipairs({ vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 0, -1), vec3(-1, 0, 0) }) do
    local forward = quat.from_euler(0, Game.yaw_of(d), 0) * vec3(0, 0, 1)
    check(forward.x * d.x + forward.z * d.z > 0.99, "yaw_of does not turn the model towards " .. d.x .. "," .. d.z)
  end
  -- the real model follows the walking direction
  if me.t == 0 then
    Game.fake_move = vec2(1, 0)
  end
  if me.t > 0.8 then
    Game.fake_move = nil
    local model = Game.player_script.model
    local forward = model:get("Transform").rotation * vec3(0, 0, 1)
    local wanted = Game.player_script.facing
    check(forward.x * wanted.x + forward.z * wanted.z > 0.95, "the warrior does not face the way he walks")
    return true
  end
end)

step("melee", function(me, dt, s)
  if me.t == 0 then
    s.enemy = Game.by_name["Enemy 1"]
    check(s.enemy ~= nil, "Enemy 1 is missing")
    s.hp = s.enemy.hp
    local p = s.enemy.transform.position
    Game.teleport(Game.player, vec3(p.x, p.y + 0.1, p.z + 1.2))
    Game.player_script.facing = vec3(0, 0, -1)
  end
  if me.t > 0.6 and not s.swung then
    s.swung = true
    Game.player_script.swing = 0
    Game.resource = 100
    check(Game.player_script:start_swing(), "the swing did not start")
    check(Game.resource == 75, "a swing should cost 25 stamina, the bar is at " .. Game.resource)
  end
  if me.t > 1.8 then
    check(s.enemy.hp < s.hp, "the sword did not hurt the yeti (hp " .. s.enemy.hp .. ")")
    -- no stamina, no swing
    Game.player_script.swing = 0
    Game.resource = 5
    check(not Game.player_script:start_swing(), "a swing without stamina should be refused")
    check(Game.resource_flash > 0, "a refused attack should flash the bar")
    check(not Game.player_script:shoot(), "the warrior should not be able to shoot")
    return true
  end
end)

step("ranged", function(me, dt, s)
  if me.t == 0 then
    Game.select_hero("wizard")
  end
  if me.t > 0.5 and not s.started then
    s.started = true
    local p = Game.player_script
    check(p.model.name == "Wizard model", "the wizard model is not shown")
    check(p.models["Warrior model"]:get("Transform").scale.x == 0, "the warrior model should be hidden")
    check(Game.resource == 100, "choosing a hero should fill the resource")
    p.shot_cd = 0
    p.facing = vec3(0, 0, 1)
    check(p:shoot(), "the shot was refused")
    check(Game.resource == 80, "a bolt should cost 20 mana, the bar is at " .. Game.resource)
    check(not p:start_swing(), "the wizard should not swing a sword")
    -- and a shot at an enemy must hurt it
    s.target = Game.by_name["Enemy 4"]
    s.hp = s.target.hp
    local e = s.target.transform.position
    Game.fire("player", vec3(e.x, e.y + 0.9, e.z + 3), vec3(0, 0, -1))
  end
  if s.started and me.t > 1.3 and not s.hurt then
    s.hurt = true
    check(s.target.hp < s.hp, "a projectile did not hurt the panda (hp " .. s.target.hp .. ")")
    -- no mana, no bolt; the mana comes back
    Game.player_script.shot_cd = 0
    Game.resource = 5
    check(not Game.player_script:shoot(), "a bolt without mana should be refused")
    check(Game.resource_flash > 0, "a refused bolt should flash the bar")
    Game.resource = 0
    Game.resource_idle = 0
    s.regen_from = me.t
    -- an exhausted pool refuses a shot
    local refused = false
    for i = 1, 16 do
      if not Game.fire("player", vec3(0, 3, 0), vec3(0, 1, 0)) then
        refused = true
      end
    end
    check(refused, "an exhausted pool should refuse a shot")
  end
  if s.regen_from and me.t > s.regen_from + 1.5 and not s.regen_checked then
    s.regen_checked = true
    check(Game.resource > 8, "the mana did not come back (" .. Game.resource .. ")")
  end
  if s.regen_from and me.t > s.regen_from + 3.2 then
    check(Game.shots.player:active_count() == 0, "projectiles did not time out")
    return true
  end
end)

step("death", function(me, dt, s)
  if me.t == 0 then
    Game.god = false
    Game.invuln = 0
    Game.set_checkpoint(vec3(5, 0.1, 8))
    Game.damage_player(1000)
    check(Game.state == "dead", "the hero should be dead")
    Game.respawn()
  end
  if me.t > 1.0 then
    check(Game.state == "playing", "respawn should resume play")
    check(Game.hp == Game.max_hp, "respawn should heal")
    check(near(Game.player_position(), vec3(5, 0.1, 8), 1.5), "the hero is not at the checkpoint")
    Game.god = true
    return true
  end
end)

step("pause", function(me, dt, s)
  local yeti = Game.by_name["Enemy 2"]
  local p = yeti.transform.position
  if me.t == 0 then
    s.start = copy(p)
    Game.teleport(Game.player, vec3(p.x, 0.1, p.z > 0 and p.z - 9 or p.z + 9)) -- far enough that it has to chase, inside the courtyard
  end
  if me.t > 1.3 and not s.paused then
    check(not near(p, s.start, 0.1), "enemy 2 should be chasing before the pause (no positive control)")
    s.paused = me.t
    Game.set_state("paused")
  end
  if s.paused and me.t > s.paused + 0.2 and s.before == nil then
    s.before = copy(p)
  end
  if s.before ~= nil and me.t > s.paused + 0.9 then
    check(near(p, s.before, 0.05), "an enemy moved while the game was paused")
    Game.set_state("playing")
    return true
  end
end)

step("kill", function(me, dt, s)
  local panda = Game.by_name["Enemy 3"]
  if me.t == 0 then
    s.left = Game.alive_in_group("courtyard")
    check(panda:take_hit(100000, vec3(0, 0, -1)), "the panda could not be hit")
  end
  if me.t > 0.2 then
    check(panda.state == "dead", "the panda should be dead")
    check(Game.alive_in_group("courtyard") == s.left - 1, "the enemy count did not drop")
    check(not panda.entity:has("CharacterController"), "a dead enemy keeps its body")
    return true
  end
end)

step("drops", function(me, dt, s)
  if me.t == 0 then
    Game.drops.health:clear() -- an enemy killed earlier may have left an orb
    Game.drops.resource:clear()
    Game.hp = 50
    check(Game.spawn_drop("health", copy(Game.player_position())), "a health orb could not be placed")
  end
  if me.t > 0.4 and not s.a then
    s.a = true
    check(Game.hp == 75, "a health orb should heal 25 (hp " .. Game.hp .. ")")
    Game.hp = Game.max_hp - 5
    Game.spawn_drop("health", copy(Game.player_position()))
  end
  if me.t > 0.9 and not s.b then
    s.b = true
    check(Game.hp == Game.max_hp, "healing must stop at the maximum (hp " .. Game.hp .. ")")
    Game.resource = 0
    Game.resource_idle = -100 -- no regeneration while this is measured
    Game.spawn_drop("resource", copy(Game.player_position()))
  end
  if me.t > 1.4 and not s.c then
    s.c = true
    check(Game.resource >= 40 and Game.resource <= 45, "a resource orb should restore 40 (" .. Game.resource .. ")")
    Game.resource_idle = 0
    -- an orb nobody picks up goes away
    s.life = Game.drops.health.life
    Game.drops.health.life = 0.4
    Game.hp = 60
    local far = copy(Game.player_position())
    Game.spawn_drop("health", vec3(far.x + 30, far.y, far.z))
    check(Game.drops.health:active_count() == 1, "the far orb is missing")
  end
  if me.t > 2.2 then
    Game.drops.health.life = s.life
    check(Game.drops.health:active_count() == 0, "an orb should expire")
    check(Game.hp == 60, "an orb that expired must not heal")
    Game.hp = Game.max_hp
    return true
  end
end)

step("bridge", function(me, dt, s)
  local plank = find_entity("Bridge link 3")
  local slider = find_entity("Sliding platform")
  check(plank ~= nil and slider ~= nil, "the bridge entities are missing")
  local z = slider:get("Transform").position.z
  s.low = math.min(s.low or z, z)
  s.high = math.max(s.high or z, z)
  if me.t == 0 then
    s.base = Game.broken_joints
  end
  if me.t > 0.5 and not s.hit then
    s.hit = true
    plank:body():add_impulse(vec3(0, -1500, 0))
  end
  if me.t > 3.0 then
    check(s.base == 0, "the bridge broke by itself (" .. s.base .. " joints) before it was hit")
    check(s.high - s.low > 0.3, "the sliding platform did not move")
    check(Game.broken_joints > s.base, "no plank of the bridge broke when it was hit")
    return true
  end
end)

step("gating", function(me, dt, s)
  local courtyard, gate = door_of("courtyard"), door_of("approach")
  if me.t == 0 then
    check(not courtyard.open and not gate.open, "the doors must be shut while enemies live")
    check(Game.alive_in_group("courtyard") > 0 and Game.alive_in_group("approach") > 0, "there should be enemies behind the doors")
    check(Game.objective():find("open the gate") ~= nil, "the objective should say to defeat the enemies (" .. Game.objective() .. ")")
    Game.teleport(Game.player, vec3(0, 0.1, -43)) -- at the gate, with the approach alive
    s.home = courtyard.home_y
  end
  if me.t > 1.5 and not s.killed then
    s.killed = true
    check(Game.loads == 0, "the gate must not load the arena while enemies live")
    check(not gate.open, "the gate opened with enemies alive")
    for _, name in ipairs({ "Enemy 1", "Enemy 2", "Enemy 4", "Enemy 5", "Enemy 6" }) do
      Game.by_name[name]:take_hit(100000, vec3(0, 0, -1))
    end
    s.killed_at = me.t
  end
  if s.killed and me.t > s.killed_at + 1.0 and not s.checked then
    s.checked = true
    check(courtyard.open, "the courtyard gate should open when its enemies are dead")
  end
  if s.killed and Game.boss_ready and not s.ready then
    s.ready = me.t
    check(Game.loads == 1, "the arena was loaded " .. Game.loads .. " times")
    check(find_entity("Dragon") ~= nil, "the dragon is not in the scene after the load")
  end
  if s.ready and me.t > s.ready + 2.0 then
    check(gate.open, "the gate of the arena should open when it is loaded")
    check(courtyard.transform.position.y < s.home - 3, "the courtyard gate did not sink")
    return true
  end
  check(me.t < 40, "the gate sequence did not finish")
end)

step("boss", function(me, dt, s)
  local dragon = Game.by_name["Dragon"]
  check(dragon ~= nil, "the dragon has no enemy script")
  if me.t == 0 then
    Game.zone = "arena"
    dragon.hp = dragon.max_hp * 0.6
    dragon:update_phase()
    check(dragon.phase == 2, "phase 2 expected at 60% health, got " .. dragon.phase)
    dragon.hp = dragon.max_hp * 0.3
    dragon:update_phase()
    check(dragon.phase == 3, "phase 3 expected at 30% health, got " .. dragon.phase)
  end
  if me.t > 1.2 and not s.killed then
    s.killed = true
    check(Game.minions_awake >= 1, "the dragon did not wake its minions")
    dragon:take_hit(100000, vec3(0, 0, 1))
    dragon:take_hit(100000, vec3(0, 0, 1))
    check(Game.state == "won", "killing the dragon should win the game")
    check(Game.wins == 1, "the win must count once, it counted " .. Game.wins)
    return true
  end
end)

step("restart", function(me, dt, s)
  if me.t == 0 then
    Game.spawn_drop("health", vec3(0, 1, 0))
    Game.start()
  end
  if me.t > 1.5 then
    local dragon = Game.by_name["Dragon"]
    check(Game.state == "playing", "restart should resume play")
    check(dragon.hp == dragon.max_hp, "restart should heal the dragon")
    check(Game.by_name["Enemy 1"].hp == Game.by_name["Enemy 1"].max_hp, "restart should heal the enemies")
    local panda = Game.by_name["Enemy 3"]
    check(panda.state == "idle" and panda.hp == panda.max_hp and panda.entity:has("CharacterController"), "restart should bring back the dead panda")
    check(Game.by_name["Enemy 7"].state == "dormant" and Game.by_name["Enemy 8"].state == "dormant", "restart should put the minions back to sleep")
    check(Game.shots.player:active_count() == 0 and Game.shots.boss:active_count() == 0, "restart should clear the projectiles")
    check(Game.drops.health:active_count() == 0, "restart should clear the drops")
    check(Game.boss == nil and Game.minions_awake == 0, "restart should clear the boss fight")
    check(not door_of("courtyard").open and not door_of("approach").open, "restart should shut the doors")
    check(near(Game.player_position(), Game.spawn, 2), "restart should return the hero to the start")
    return true
  end
end)

step("options", function(me, dt, s)
  -- game options: applied and stored
  Options.set_option("sensitivity", 1.7)
  check(Game.opt.sensitivity == 1.7 and prefs.get("opt_sensitivity", 0) == 1.7, "a game option is not stored")
  Options.set_option("sensitivity", 1.0)
  Options.set_option("difficulty", 3)
  check(Game.diff().damage == 1.5 and Game.diff().name == "Hard", "the difficulty did not change")
  Options.set_option("difficulty", 2)

  -- graphics: through the engine settings API, then put back as they were
  local before = settings.graphics.get()
  local look = settings.scene.get()
  local aa = Options.gfx.aa
  Options.set_graphic("aa", 1)
  check(settings.graphics.get().fxaa and not settings.graphics.get().taa, "FXAA was not applied")
  Options.set_graphic("aa", 2)
  check(settings.graphics.get().taa and not settings.graphics.get().fxaa, "TAA was not applied")
  Options.set_graphic("bloom", not look.bloom)
  check(settings.scene.get().bloom == not look.bloom, "the bloom option did not reach the scene")
  Options.set_graphic("aa", aa)
  Options.set_graphic("bloom", look.bloom)
  local after = settings.graphics.get()
  check(after.fxaa == before.fxaa and after.taa == before.taa and settings.scene.get().bloom == look.bloom, "the graphics settings were not put back")

  -- the options screen opens from the pause menu and goes back to it
  Game.set_state("paused")
  Options.open()
  check(Game.state == "options", "the options screen did not open")
  Options.show_tab("controls")
  Options.show_tab("game")
  Options.close(true)
  check(Game.state == "paused", "the options screen should return to the pause menu")
  Game.set_state("playing")
  return true
end)

function Selftest:on_init()
  self.index = 1
  self.t = 0
  self.s = {}
  self.finished = false
end

function Selftest:on_update(dt)
  if self.finished or not prefs.get("smoke", false) then
    return
  end
  local current = steps[self.index]
  if current == nil then
    self.finished = true
    log("GameSmoke", "GAMESMOKE PASS")
    return
  end
  local ok, result = pcall(current.fn, self, dt, self.s)
  if not ok then
    self.finished = true
    log("GameSmoke", "GAMESMOKE FAIL: " .. current.name .. ": " .. tostring(result))
    return
  end
  if result then
    log("GameSmoke", "step " .. current.name .. " ok")
    self.index = self.index + 1
    self.t = 0
    self.s = {}
  else
    self.t = self.t + dt
  end
end

return Selftest
