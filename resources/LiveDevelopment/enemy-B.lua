local enemy = ...
if enemy.maxHp > 0 and enemy.hp <= enemy.maxHp * 0.5 then
    return 1.5
end
return 1.0
