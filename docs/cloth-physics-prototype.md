# 揺れ物物理の試作

## 起動・操作

Debug構成でビルドする。タイトルの `Title Lighting` にある **Cloth Physics Showroom**、または既存Showroomの武器編集画面にある同名ボタンから開ける。マウスがゲームに捕捉されている場合はEscで解除する。

直接起動する場合、プロジェクトディレクトリを作業ディレクトリにして以下を実行する。

```powershell
../generated/outputs/Debug/CG2_Setup.exe --cloth-showroom
```

`Model` でMyGtYUhe6t／emaを切り替える。`Procedural motion` でIdle／Walk／Runを選ぶ。`Move character` で左右移動による慣性を確認できる。`Character direction` と `Orbit camera` で身体の向き・視点を変え、側面から脚とスカートの接触を観察できる。

`Physics enabled` はモデル全体、各グループの `Enabled` は個別の切り替え。各グループにGravity／Damping／Constraint iterations／Stiffnessを用意した。Stiffnessはアニメーション姿勢へ戻る力であり、距離Constraint自体の硬さは種類ごとに保持している。`Reset cloth` で現在の姿勢に戻す。Pause時は物理時間を進めない。

デバッグ表示は画面上への投影によるワイヤー表示で、深度テストを行わない。身体内部のColliderも確認できる。

- オレンジ：Sphere／CapsuleとCollider名
- 黄：固定Particle（ボーン列の根元）
- 緑：自由Particle（ボーンの位置と仮想毛先）
- 水色：縦方向・曲げ・横方向の距離Constraint

`Body colliders` では各Colliderの有効状態と半径を調整できる。画面での調整はメモリ内のみで、モデル切り替え時はそのモデルの設定を保持する。`Reload bone profile` はJSONを読み直すため、ファイル編集後も再起動は不要。

## モデル構造の調査結果

`tools/inspect-cloth-models.py` でPMXのMaterial、Bone、親子関係、各Boneの頂点ウェイト数を読み取る。結果は `generated/cloth/model-inventory.json` に出力する。元アセットには書き込まない。

MyGtYUhe6tのキャラクターは `安比.pmx`（24,419頂点、233ボーン、28 Material）。Materialに `裙`、`裙饰`、`髮` があり、スカートは `裙_0_0`〜`裙_4_15` の16列×5段のボーン構造を持つ。髪には `HairSL_*`、`HairSR_*`、`hair1-*`、`hair2-*` などがある。`anbi.glb` にはMMDの剛体表示用Meshも含まれ、アニメーションは0本。今回のShowroomでは元PMXの頂点ウェイトとMaterialを利用する。

emaのスカートは `Skt_0_0`〜`Skt_5_11` の12列×6段。服のMaterialは上着・装飾等も含んでいるため、Material全体を物理対象にせずボーン名を指定する。髪には `SdHair_*`／`FrHair_*`、リボンには `ChestRb_*`／`HairAcsC_*` がある。全ボーンを一度に物理化せず、明示した数列で試作する。

どちらのモデルも脚の表示頂点は主に `左足D`／`左ひざD`／`左足首D` と右側の対応ボーンにウェイトされている。そのためColliderの `LeftUpperLeg` 等はこれらへ結び付けている。PMXの `左足` 等とは別のノードなので、確認用FKは両系列を駆動する。

PMXにもGLBにも使用可能なIdle／Walk／Runクリップはない。Showroomの動作は確認用の手続き的FKであり、元モデルの歩行モーションではない。PMXのIK・付与親（grant）・既存MMD剛体設定は今回評価していない。

## 構成と処理順

- `Engine/Physics/ClothSolver.*`：PhysicsParticle、PhysicsConstraint、PhysicsColliderとVerlet／PBD処理。描画やModelから独立した計算部分。
- `Engine/Physics/ClothComponent.*`：ボーン列のバインド、身体Collider、物理結果のボーン姿勢への反映、ImGui。
- `resources/physics/anby.json`／`ema.json`：モデル別の明示的なボーン選択とパラメータ。
- `Game/scene/Main/ClothShowroomScene.*`：モデル・動作・視点の切り替え。

Object3dの通常アニメーション／手動姿勢を評価 → インスタンスの姿勢変更フック → ボーンの現在位置からColliderと固定点を更新 → 物理演算 → ボーン行列を更新 → 既存のSkinCluster／GPU Skinningという順序。共有Modelや元の頂点バッファ、Animationには物理結果を書き込まない。フック未設定のObject3dは従来の処理を使う。

各ボーン列の先頭位置を固定し、下流位置を自由Particleとして扱う。最後には仮想Particleを追加して最終ボーンも回転できるようにする。スカートには環状の横Constraintと曲げ用の距離Constraintを追加する。ボーンの回転はアニメーション姿勢からParticleの方向への最短回転、位置はParticle座標から求める。選択していない子ボーンは既存のローカル姿勢を保って追従する。

120Hzの固定ステップ、最大12ステップ／更新。前後フレームの固定点とColliderを補間する。重力・減衰・姿勢への復元力を加え、距離Constraintと衝突投影を反復する。Sphere／Capsuleの内部から半径＋Particle半径の外側へ押し戻す。中心・軸上の衝突やゼロ長Capsuleも処理する。有効状態変更と大きな位置移動では履歴をリセットする。

## 別モデルへの指定

JSONの `groups[].chains` に**正確なボーン名の列**を設定する。各列の先頭が固定点になる。スカート等の環状配置には `closedRing: true` を設定する。同じボーンを複数の列へ指定すると読み込みエラーになるので、枝分かれは独立した部分列として指定する。

Colliderは `start` と `end` のボーン、`radius`（モデル座標単位）、`shape`（Sphere／Capsule）を指定する。Sphereでは `start` を使う。任意の `startOffset`／`endOffset`（ボーンローカル座標）も使える。腰・肩・腕等も同じ形式で追加可能。今の適用部分は一様な正のモデルScaleを想定する。

名前が不一致の場合は物理対象を推測せず、画面に `Missing bone` を表示して通常のモデル姿勢へ戻す。今回の実装はボーン方式であり、揺れ物用ボーンがないMeshの頂点Cloth化はまだ行っていない。

## 検証

```powershell
./tools/build.ps1 -Configuration Debug
./tools/test-cloth-solver.ps1
./tools/test-cloth-runtime.ps1
./tools/test-showroom.ps1
```

計算部分のテスト：固定点、復元、距離保持、Sphere、Capsule軸上／ゼロ長、脚相当Colliderの押し戻し、ON／OFF、位置リセット、不正dt、30／60fpsの固定時間処理。

実モデルテスト：両PMXのプロファイル読込、Idle／Walk／Run各240フレーム、固定点、全Collider外部への投影、ボーン追従、物理Particleから描画姿勢への反映、共有Modelの不変、物理OFFの姿勢復元、再有効化、実際のD3D12描画・PNG保存。

画像・実測値・成否は `generated/cloth-tests/` に出力する。`*-debug-workspace.png` はImGuiとデバッグ線を含む画面、その他の画像はエンジンの3D描画結果。接触回数は反復ごとの投影回数であり、独立した衝突イベント数ではない。

初回の実測はAnbyで接触投影364,385回、最大変位0.1660、距離Constraintの最大誤差0.00080。emaで接触投影66,405回、最大変位0.1587、距離Constraintの最大誤差0.00133。自由ParticleのCollider内侵入量は両モデルとも1e-7未満だった（値はエンジンのワールド座標）。Debug／Releaseビルド、計算テスト、実モデルのGPUテスト、既存Showroomテストが成功した。側面の物理ON／OFF画像とデバッグ画面を確認している。

## 制約と次の改善

脚とスカートの**ボーンParticle間**の接触は確認済み。ただし頂点や三角形を直接衝突させていないため、Particle間の面や厚い装飾には部分的な貫通が残り得る。自己衝突、連続衝突判定、布の厚み、布同士の接触も未対応。極端な瞬間回転や高速な動作は試作範囲を超える。

次はモデルに合わせたCollider形状／半径の調整、実際の歩行クリップ、脚IKやPMX付与親の扱い、ボーン区間／頂点サンプルの衝突を追加するとよい。長いマントやボーンのない服には頂点ParticleとMesh／Material範囲指定、密な髪の枝分かれには共有固定点や専用の曲げ・角度Constraintが必要になる。今回は外部の大型物理ライブラリを追加していない。
