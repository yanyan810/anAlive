#pragma once
#include "Player.h"
#include "Enemy.h"
#include "EnemyProjectile.h"
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Compile-time checks also apply to non-const owners: reading must not grant writes.
static_assert(std::is_same_v<decltype(std::declval<Player&>().CurrentWeapon()), const WeaponRuntime&>);
static_assert(std::is_same_v<decltype(std::declval<Player&>().Settings()), const FPSMotion::Settings&>);
static_assert(std::is_same_v<decltype(std::declval<Enemy&>().Senses()), const EnemySenses&>);
static_assert(std::is_same_v<decltype(std::declval<Model&>().GetMaterial()), const Model::Material*>);
static_assert(!std::is_reference_v<decltype(std::declval<Model&>().GetMaterialColor())>);
static_assert(std::is_same_v<decltype(std::declval<EnemyProjectileSystem&>().Projectiles()), const std::vector<EnemyProjectile>&>);

inline void RunEncapsulationChecks() {
    const auto check=[](bool ok) { if (!ok) throw std::runtime_error("Encapsulation boundary regression"); };
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float infinity=std::numeric_limits<float>::infinity();
    Camera camera;
    const auto projection=camera.GetProjectionMatrix();
    for (float bad : {-1.0f, 0.0f, nan, infinity}) {
        camera.SetFovY(bad); camera.SetAspect(bad); camera.SetNearZ(bad); camera.SetFarClip(bad);
    }
    camera.SetFovY(std::numbers::pi_v<float>);
    camera.SetNearZ(1000); camera.SetFarClip(.1f); camera.SetOrthographic(true,nan);
    camera.Update();
    for (int row=0;row<4;++row) for (int col=0;col<4;++col)
        check(camera.GetProjectionMatrix().m[row][col]==projection.m[row][col]);
    camera.SetFovY(1); camera.SetAspect(2); camera.SetNearZ(.05f); camera.SetFarClip(600); camera.Update();
    check(camera.GetFovY()==1 && camera.GetProjectionMatrix().m[0][0]!=projection.m[0][0]);
    Player player;
    player.ApplyDamage(-1); player.ApplyDamage(nan); player.ApplyDamage(infinity);
    check(player.GetHP()==100 && !player.IsDead());
    player.ApplyDamage(25); check(player.GetHP()==75);
    player.ApplyDamage(1000); player.ApplyDamage(10); check(player.GetHP()==0 && player.IsDead());
    auto movement=player.Settings(); movement.moveSpeed=8;
    check(player.ConfigureMovement(movement) && player.Settings().moveSpeed==8);
    movement.cameraHeight=nan;
    check(!player.ConfigureMovement(movement) && player.Settings().cameraHeight==FPSMotion::Settings{}.cameraHeight);
    movement=player.Settings(); movement.moveSpeed=-1;
    check(!player.ConfigureMovement(movement) && player.Settings().moveSpeed==8);

    Enemy enemy;
    auto senses=enemy.Senses(); senses.fieldOfView=360; senses.memoryRecordInterval=0;
    check(enemy.ConfigureSenses(senses));
    senses.viewDistance=nan; check(!enemy.ConfigureSenses(senses) && std::isfinite(enemy.Senses().viewDistance));
    senses=enemy.Senses(); senses.fieldOfView=361; check(!enemy.ConfigureSenses(senses));
    senses=enemy.Senses(); senses.searchDirectionPriority=-1; check(!enemy.ConfigureSenses(senses));

    WeaponDefinition definition; definition.id="encapsulation"; definition.magazineSize=3;
    definition.reserveAmmo=6; definition.maxReserveAmmo=6; definition.reloadTime=1;
    check(player.EquipWeapon(definition));
    WeaponRuntime weapon=player.CurrentWeapon();
    check(weapon.TryFire() && weapon.Magazine()==2 && weapon.StartReload());
    auto invalid=definition; invalid.ammoPerShot=0;
    check(!weapon.Equip(invalid) && weapon.Magazine()==2 && weapon.Reloading());
    invalid=definition; invalid.magazineSize=-1; check(!weapon.Equip(invalid));
    invalid=definition; invalid.reserveAmmo=-1; check(!weapon.Equip(invalid));
    invalid=definition; invalid.burstCount=0; check(!weapon.Equip(invalid));
    invalid=definition; invalid.fireMode=static_cast<WeaponFireMode>(99); check(!weapon.Equip(invalid));
    for (float bad : {-1.0f, nan, infinity}) {
        invalid=definition; invalid.reloadTime=bad; check(!weapon.Equip(invalid));
        invalid=definition; invalid.burstInterval=bad; check(!weapon.Equip(invalid));
    }
    check(weapon.Magazine()==2 && weapon.Reserve()==6 && weapon.ReloadRemaining()==1);
    weapon.Update(1); check(weapon.Magazine()==3 && weapon.Reserve()==5 && !weapon.Reloading());
    player.RestoreWeaponForDebug(weapon);
    check(player.CurrentWeapon().Reserve()==5);
    player.UnequipWeapon(); check(player.CurrentWeapon().Definition().id.empty() && player.CurrentWeapon().Magazine()==0);

    EnemyProjectileSystem projectiles;
    EnemyProjectile projectile; projectile.velocity={0,0,10}; projectile.damage=10;
    check(projectiles.Spawn(projectile));
    projectile.position.x=nan; check(!projectiles.Spawn(projectile) && projectiles.Projectiles().size()==1);
    check(projectiles.Update(.1f,{100,0,0})==0 && projectiles.Projectiles()[0].position.z==1);
    projectiles.Clear(); check(projectiles.Projectiles().empty());
}
