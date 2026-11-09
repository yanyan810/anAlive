# 敵の階段追跡

敵の経路探索に床・傾斜面の高さを含めました。以前は目標との高低差が0.3mを超えると `HeightDifference` で経路を作れず、階段の入口で停止していました。

- 足元の床を短い区間ごとに調べ、ホテルのSmoothRampと踊り場に沿って移動します。
- 同じXZでも高さの違う床を別ノードとして探索します。階段がない場所で上下階へ移動することはできません。
- 上方向0.35m・下方向0.5mの局所的な足場接続のみ許可し、高い段差や床のない場所を飛び越えません。壁・低い天井には体の大きさで衝突判定します。
- プレイヤーは坂道で足元の複数点を接地させるため、中心の床より少し高くなります。目標の接地位置を床へ投影し、この差で追跡が止まることを防ぎます。
- 敵同士の押し合いも床へ追従させ、別の階にいる敵同士は押し合いません。視界・警戒伝達・壁越しの攻撃禁止は維持します。

ステージの形状、階段、敵の大きさは変更していません。Player用の移動は従来の処理を使用します。

検証:

```powershell
./tools/test-enemy-senses.ps1
./tools/build.ps1 -Configuration Debug
./tools/test-hotel-enemy-stairs.ps1 -Configuration Debug
./tools/test-hotel-combat-placement.ps1 -Configuration Debug
./tools/build.ps1 -Configuration Release
./tools/test-hotel-enemy-stairs.ps1 -Configuration Release
./tools/test-hotel-combat-placement.ps1 -Configuration Release
```

単体検証では階段途中、両階段の上り下り、3階客室から1階までの経路、30/60/144FPS、階段のない上下階と高い段差の拒否を確認します。実GameSceneでは坂道上のPlayerへの接近、Playerがさらに下がった後の再追跡、客室サイズのTank、敵3体の同時下降、床・壁・天井の衝突を確認します。位置・高さの記録とGPU画像は `generated/enemy-stairs-tests/<Configuration>/` に出力します。
