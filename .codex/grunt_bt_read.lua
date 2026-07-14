local path = "/Game/Core/Enemies/AI/BT_EnemyCombat_Grunt"
print("=== " .. path .. " ===")
local asset = open_asset(path)
local nodes = asset:list("nodes")
print(nodes)
