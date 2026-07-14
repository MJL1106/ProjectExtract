local paths = {
  "/Game/Core/Enemies/AI/BT_EnemyBase",
  "/Game/Core/Enemies/AI/BT_EnemyCombat_Grunt",
  "/Game/Core/Enemies/AI/BB_Enemy",
  "/Game/Core/Enemies/AI/Data/DA_Enemy_Grunt",
  "/Game/Core/Enemies/AI/Data/DA_Enemy_Officer",
  "/Game/Core/Enemies/AI/EQS/EQS_FindCover"
}

for _, path in ipairs(paths) do
  print("=== " .. path .. " ===")
  local ok, asset = pcall(open_asset, path)
  if not ok or asset == nil then
    print("OPEN_ERROR " .. tostring(asset))
  else
    local okHelp, helpText = pcall(function() return asset:help() end)
    print("HELP " .. tostring(okHelp) .. " " .. tostring(helpText))
    local okNodes, nodes = pcall(function() return asset:list("nodes") end)
    print("NODES " .. tostring(okNodes) .. " " .. tostring(nodes))
    local okKeys, keys = pcall(function() return asset:list("keys") end)
    print("KEYS " .. tostring(okKeys) .. " " .. tostring(keys))
  end
end
