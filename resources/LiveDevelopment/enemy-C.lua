local enemy = ...
if enemy.maxHp > 0 and enemy.hp <= enemy.maxHp * 0.5 and enemy.state == "Chase" then
    return 2.0
end
return 1.0
