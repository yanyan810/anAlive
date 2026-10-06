# 揺れ物物理の試作

## 起動・操作

Debug構成でビルドする。タイトルの `Title Lighting` にある **Cloth Physics Showroom**、または既存Showroomの武器編集画面にある同名ボタンから開ける。マウスがゲームに捕捉されている場合はEscで解除する。

直接起動する場合、プロジェクトディレクトリを作業ディレクトリにして以下を実行する。

```powershell
../generated/outputs/Debug/CG2_Setup.exe --cloth-showroom
```

`Model` でMyGtYUhe6t／emaを切り替える。`Procedural motion` でIdle／Walk／Runを選ぶ。`Move character` で左右移動による慣性を確認できる。`Character direction` と `Orbit camera` で身体の向き・視点を変え、側面から脚とスカートの接触を観察できる。

髪を拡大する場合は`Camera target height`を約1.8、`Camera distance`を約1.3にし、`Follow character camera`を有効にする。カメラだけがキャラクターの水平位置へ追従し、身体の移動と物理演算は続ける。

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

前開きの服では `closedRing: true` と任意項目 `openSeams: [[0, 1]]` を併用できる。数値は `chains` の0始まりの列番号で、指定した隣接列間の横Constraintと横Collision Sampleを生成しない。最後の列と0列も隣接扱い。範囲外・同一列・非隣接の指定は読み込みエラーにする。項目がなければ従来の完全な環状接続を維持する。Meshからの自動推定は行わない。

Colliderは `start` と `end` のボーン、`radius`（モデル座標単位）、`shape`（Sphere／Capsule）を指定する。Sphereでは `start` を使う。任意の `startOffset`／`endOffset`（ボーンローカル座標）も使える。腰・肩・腕等も同じ形式で追加可能。今の適用部分は一様な正のモデルScaleを想定する。

名前が不一致の場合は物理対象を推測せず、画面に `Missing bone` を表示して通常のモデル姿勢へ戻す。今回の実装はボーン方式であり、揺れ物用ボーンがないMeshの頂点Cloth化はまだ行っていない。

## 検証

### Collision Sampleによる表示Meshの貫通対策

既存のボーンParticle・Skinning・Verlet/PBDを維持し、隣り合うParticle間に衝突判定専用のSampleを追加した。Sampleはボーンや自由Particleではなく、両端の補間位置を使う。縦区間とclosedRingの横区間を明示しており、曲げConstraintの対角線には生成しない。末端の仮想Particleまでの区間も対象になる。

各グループのJSONに次の任意項目を追加できる。

```json
"collisionSamplesPerSegment": 2,
"collisionSampleRadius": 0.02,
"enableHorizontalCollisionSamples": false
```

Sample数2は区間内部の1/3、2/3に生成する意味で、区間を3分割する。数は0〜8、半径は既存Particleと同じワールド座標単位。項目がない場合は数0、横方向無効となり、旧Solverと同じ挙動になる。初期のSample対応ではemaのJSONは変更せず検証したが、現在は下記のドロワ対策を設定している。

Sampleが押し戻されると、両端の位置だけでなくVerlet履歴にも補正を返す。A/Bの形状重みを `u=1-t`、`v=t`、逆質量を `wA/wB` とすると、補正係数は `wA*u/(wA*u*u+wB*v*v)` と `wB*v/(wA*u*u+wB*v*v)`。この正規化によって補間点が必要量だけ動き、固定端の逆質量0には補正が加わらない。接触を距離Constraintと交互に反復する。

MyGtYUhe6tのSkirtには縦Sampleを各2個、Sample半径0.02を設定した。160個の縦Sampleで、描画ボーン数は増えない。横Sampleを有効にするとさらに160個を追加する。脚CapsuleのUpperLeg半径は0.63→0.72、LowerLegは0.43→0.49（モデル座標単位）。左右・上下の半径は従来どおりImGuiで別々に調整できる。Hip等の追加も従来のCollider指定で可能だが、今回のプリセットでは既存6Colliderを維持した。

`Collision Sample display` は独立した表示切り替え。紫は接触なし、ピンクは最後の物理更新中に押し戻されたSample、輪郭のみの点は横Sample。表示位置は補正後の両端を補間し直した位置であり、SampleだけがMeshから離れて動いた位置ではない。Skirtのパネルで数・半径・横方向有効状態を変更できる。`Gait amplitude` は検証用FKの歩幅倍率で、従来どおりの動きは1、確認用の大きめWalkは1.35。

実モデルテストは「旧半径・Sampleなし」「半径調整のみ」「縦Sample」「縦＋横Sample」を同じ1.35倍Walkで比較する。各2秒間の20姿勢を測定し、同じ姿勢の正面・側面・斜め画像を保存する。旧半径のCapsuleだけを基準にした表示Meshの侵入検査では、旧実装にも侵入ゼロが出てしまうため、テスト専用のCPU Skinningと三角形交差診断も追加した。これは実行時のCloth衝突処理には使用しない。

比較対象は表示スカートMaterial `裙` と、脚／下半身にウェイトされた肌・靴下・脚飾りのMesh。内部のショートパンツ `裤` だけとの交差は別集計する。1枚のスカート三角形が複数の脚三角形へ交差しても、その姿勢では1回と数える。接する共有端点・同一平面の重なりは含めず、見た目の貫通が完全になくなることを保証する指標ではない。

結果と残る交差の分類は `generated/cloth-tests/segment-comparison.csv` と `metrics.txt` に記録する。代表交点が全脚Capsuleの半径＋Sample半径より外にある場合はCollider形状による未被覆、その範囲内だがSample区間から離れている場合はSkinningのMesh/Boneの差や未評価の三角形内部、区間のSample半径内に残る場合はSample間隔の不足が疑われる、という切り分けを行う。

同じ20姿勢の脚Meshとの交差三角形数の合計は以下のとおり。交差した画素数や侵入の深さ、独立した衝突イベント数ではない。

| 設定 | 脚Meshとの交差 | 内部ショートパンツだけとの交差 |
| --- | ---: | ---: |
| 旧半径・Sampleなし | 1,127 | 1,305 |
| 半径調整のみ | 637 | 1,183 |
| 半径調整＋縦Sample | 451 | 970 |
| 半径調整＋縦横Sample | 451 | 949 |

縦Sampleの設定で旧実装から約60%、半径調整のみから約29%減った。正面・側面・斜めの画像でも、スカート途中から脚が見える箇所の減少を確認した。横Sampleはショートパンツとの交差を少し減らしたが、今回の設定では脚Meshの集計は同じだったため、標準では横方向を無効にした。

残る451件の代表交点はすべて、調整済み脚CapsuleにSample半径0.02を加えた範囲の外側だった。今回の診断からは**脚・下半身の表示Meshに対するCollider形状の被覆不足**が残る交差の主因と考えられる。代表点による離散検査なので、ボーン密度、Skinningでの面のずれ、三角形内部の未評価まで完全に否定する結果ではない。まず腰付近や脚の外形に合わせたCapsuleのオフセット・追加Sphereを調整し、その範囲内に残る交差を再検査するのが次の段階になる。

最終プリセット（縦160 Sample）でIdle／Walk／Runを各240フレーム検証し、Anbyの自由Particleの最大侵入量は2.98e-8、Sampleの最大侵入量は1.57e-7、構造距離誤差は0.000899だった。emaはSampleを生成せず従来設定で検証し、自由Particleの最大侵入量6.71e-8、構造距離誤差0.001321で成功した。Debug／Releaseビルドは警告・エラーとも0。Cloth Solver、両モデルのCloth GPU実行テスト、既存Showroomテストがすべて成功した。

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

## emaのドロワと表示中の負荷の追加修正

emaの表示スカートは`Mt_SakurabaEma_Clothes01`と`Mt_SakurabaEma_Clothes01_Stencil`のうち`Skt_*`へウェイトされた部分。ドロワは`Mt_SakurabaEma_Clothes02`のうち脚／下半身へウェイトされた部分で、Material全体をスカートとみなすと正しい診断にならない。元PMXを調べるとドロワの太もも中心線からの最大距離は約1.044モデル単位だった。旧UpperLeg半径0.63ではこの外周を覆えず、Sampleも無効だった。

最初のドロワ対策ではUpperLegを左右とも1.10、LowerLegを0.48へ調整し、下半身に半径0.90のHip Sphere（ローカルYオフセット-0.20）を追加した。Skirtは縦・横とも内部Sample2個、Sample半径0.035ワールド単位として検証した。ただし脚全体を太く覆い、前開きまで横Constraintで接続していたため、腰付近が横へ広がりすぎた。現在の設定は次節の再調整を使う。

この最初の対策では、同じ1.35倍Walkの2秒間を12フレームごとに測定した10姿勢で、表示スカートとドロワの交差三角形数の合計は旧設定1,136→0になった。Sample半径だけを0.025へ下げると62件が残り、その代表交点はすべて脚Capsule＋Sample半径の外にあった。0.035で外側の余裕を増やして再検証した。添付画像と同じCharacter direction -103度・Gait amplitude 1の10姿勢でも0。正面・側面・斜めの実GPU画像を確認した。

これは離散的な三角形交差診断であり、接線・同一平面・全時刻／全モーションの非貫通を保証する検査ではない。元モデルの前側の開口からドロワが見える部分は残る。元Mesh・Material・ウェイトを変更／非表示にしていない。この最初の設定ではIdle／Walk／Run各240フレームでemaの自由Particle侵入は0、Sample侵入は1.49e-7、構造距離誤差は0.001660だった。

表示中の処理負荷には、Clothの確実な接触外の点をAABB・距離の二乗で早期除外し、Sampleの過去位置・目標位置の補間を接触時まで遅らせた。距離Constraint係数は更新内で再利用し、Colliderの時間補間は反復の外へ移した。120Hz・反復数・Particle／Sample密度は削っていない。Debugの操作性のため、`ClothSolver.cpp`だけ`/O2`でコンパイルする。アサートとシンボルは保持するが、このファイルの最適化されたコードではステップ実行が通常のDebugほど細かく追えない。他のDebugファイルの最適化設定は変更していない。

Animatorは毎フレームのSkeleton全体のコピーをやめ、変更可能なTransformだけ復元する。既定の手動Transformには回転計算を行わない。特にGPU Upload Heapの書き込み結合メモリから行列を読み返して逆行列を計算していた箇所を、通常のCPUメモリで全計算してから書き込む形に変更した。この改善はCloth以外のSkinningモデルにも適用される。手動姿勢の非累積、ポーズ変更後の復元、実Clipの同時刻再評価も読み込みテストで検証する。

実モデルのDebug CPU更新を240フレーム計測した平均（Walk、歩幅1.35、モデル表示・非表示の変更なし）：

| モデル | 修正前のCPU更新 | 修正後のCPU更新 | Cloth部分・修正前→後 |
| --- | ---: | ---: | ---: |
| MyGtYUhe6t | 34.39ms | 1.78ms | 22.00→1.44ms |
| ema | 27.53ms | 2.71ms | 6.69→2.21ms |

emaは修正後にCollision SampleとHipを追加した状態で比較している。読み込みやGPU描画・Present待機を含む数値ではなく、FPSそのものではない。単発の同環境測定なのでPC・表示設定によって変動する。Showroom左上の`CPU update ... (cloth ...)`でもこの内訳を確認できる。

```powershell
./tools/test-cloth-performance.ps1 -Label current
```

`generated/cloth-tests/performance-*.csv`へCPU更新、Cloth、別計測のDraw＋Present時間を保存する。後者にはVSync・フレーム制限・非表示ウィンドウの待機も含まれ、純粋なGPU時間ではない。MyGtYUhe6tの接触回数・構造誤差・交差数は高速化前と同じだった。Debug／Releaseビルド、Cloth Solver・GPU実モデル・既存Showroom・タイトルのGPU描画・両構成の読み込み／ポーズ復元テストを再検証した。キャッシュ再利用時のモデルデータも以前のbaselineと一致した。

## emaの横への広がりを抑える再調整

元PMXのスカート三角形と頂点ウェイトを調べると、隣接ボーン列0と1の両方へウェイトされた三角形は0枚で、ここが前開きだった。他の隣接列間には319〜438枚あり、11→0も350枚の接続がある。ボーンの番号順を完全な輪として扱うと、存在しない前開きの面を横Constraintで縫い合わせ、脚に押された変位を服全体へ伝えてしまう。emaのSkirtに `openSeams: [[0, 1]]` を指定して、この区間の横Constraint6本・横Sample12個だけを外した。11→0と他の接続は残す。Solver・Particle・描画ボーンの構成は維持した。

脚全体のUpperLeg半径を1.10→0.65へ戻し、ドロワを覆う短いCapsuleを左右に追加した。同じ上脚ボーンのローカルY=-0.85〜-1.55へ配置し、Xを左右それぞれ外側へ0.12ずらし、半径1.05とした。上端はY=-0.40・外側X=0.10・半径0.93のSphereで補う。LowerLeg 0.48、Hip 0.90は維持し、計11Colliderが毎フレーム身体ボーンへ追従する。SkirtのSampleは縦・横とも2個を維持し、半径を0.035→0.025ワールド単位へ下げた。半径とSample設定は従来のImGuiで変更できる。

静止時は物理OFFの同じIdle姿勢を基準に、ON後300フレームの最大幅を比較する。胴回りの膨らみを見るため、モデルの元座標Y=9.5〜11.5の頂点範囲も測定する。正面・キャラクター方向0度で測定した値（ワールド単位）：

| 部分 | 物理OFF | 調整後Idleの最大幅 | 以前の太い設定・Walk | 調整後Walk |
| --- | ---: | ---: | ---: | ---: |
| スカート全体 | 0.546822 | 0.546038 | 0.561065 | 0.567672 |
| 腰〜服の中段 | 0.386555 | 0.401600 | 0.462404 | 0.412701 |

静止時の裾幅は基準との差0.2%未満、中段は約3.9%の増加。強めのWalkで中段の最大幅は以前の太い設定から約10.7%狭くなった。裾の揺れは残し、腰付近の横への過剰な膨らみを抑えている。

1.35倍Walkの10姿勢、および報告画像と同じ方向-103度・歩幅1の10姿勢を検査し、調整後の表示スカートとドロワの交差三角形数はどちらも0だった。`ema-drawers-comparison.csv`には旧設定を再現した`before`、以前の太い設定`wide`、今回の`fitted`、報告画像の方向`screen-angle`を保存する。`ema-bind-front.png`、`ema-idle-front.png`、`ema-idle-back.png`と、`ema-fitted-{front,side,oblique}.png`で実D3D12描画も確認した。

回帰テストは前開き0→1が接続されないこと、11→0が残ること、項目省略時に元の環状接続へ戻ること、不正な非隣接列指定が失敗することを検査する。Idle中の過剰な幅増加、Walkでの膨らみの再発、検査姿勢でのドロワ交差にも失敗条件を追加した。新しい`openSeams`処理には今回ビルドした実行ファイルが必要なので、一度アプリを再起動する。その後のJSON調整は`Reload bone profile`で反映できる。

今回の最終設定でDebug CPU更新を再測定すると、MyGtYUhe6tは平均1.15ms（Cloth 0.93ms）、emaは2.08ms（Cloth 1.76ms）だった。`performance-final-shape.csv`に保存する。Debug／Releaseビルドは両方とも警告・エラー0、両モデルのDebug実GPUテスト、Cloth Solver、既存Showroomテストが成功した。Cloth Showroom／実モデルテストの起動口はDebug専用であり、Releaseの実モデルテストは実施していない。

## WalkからRunへの切り替えとアンビーの前髪

以前のShowroomはモード変更時にClothをリセットし、歩行位相を`time * 選択した周波数`から求めていた。Walk中の時刻へ突然Runの周波数を掛けると、脚の姿勢が飛び、服の接触姿勢も失われる。現在は位相を時間積分し、周波数・歩幅・膝の曲げ・上半身の傾き・移動量を時定数0.18秒で補間する。モード選択ではClothをリセットせず、明示的なReset、モデル切り替え、テストの時刻初期化では従来どおり初期化する。これは確認用FKの変更であり、通常のAnimation評価は変更していない。

アンビーの前髪は、ParticleがColliderの外にあっても、根元ボーンの回転とSkinningのオフセットで表示Meshが額へ入る場合があった。また従来の減衰はワールド速度全体を減らしていたため、移動する頭に対して髪が大きく遅れた。次の任意設定をHairへ追加した。

```json
"dampingRelativeToAnimation": true,
"maxSwingAngleDegrees": 10,
"collisionSamplesPerSegment": 2,
"collisionSampleRadius": 0.012
```

相対減衰では、補間されたアニメーション目標の1ステップ分の移動を速度から引いて減衰し、再び足す。等速移動そのものを減衰せず、加速や方向転換の慣性を残す。角度制限は縦区間の方向を、そのステップのアニメーション方向を中心とする円錐へ投影する。両端へ逆質量で分配し、固定点は動かさず、位置補正による人工的な速度も加えない。距離Constraintと交互に適用し、その後のCollider接触を優先する。未指定では相対減衰false、角度180度（制限なし）となるため、旧JSONの挙動は維持する。ImGuiのグループパネルでも変更可能。

HairのGravityは-1.2、Damping 0.28、Stiffness 0.90とした。16縦区間へ計32Sampleを追加し、既存Head Sphereに加えて`Forehead` Sphereを頭ボーンのローカル座標`[0, 1.2, -0.45]`、半径0.85モデル単位へ配置した。Colliderは計7個になり、半径は既存のBody collidersパネルで調整できる。スカートのCollider、開口、Particle構成は維持している。

実モデルテストは継続Runを240フレーム、4つの異なるWalk位相からの切り替えを各120フレーム検証する。歩幅1.35、慣性移動ON、キャラクター方向0度／±90度を使い、12フレームごとに実表示MeshをCPU Skinningして検査する。正面・側面・斜めと、拡大した髪の実D3D12画像も保存する。

- 切り替え直後の膝の最大移動：以前はアンビー0.632／ema 0.591、現在は0.055／0.052ワールド単位。これは1フレームの移動であり、布の最大変位とは別の指標。
- emaの服と胴体の交差：継続Runの20姿勢、切り替え後の40姿勢でともに0。従来のドロワ対策とIdleの裾幅・腰幅の回帰テストも成功。
- アンビーの元モデルにも生え際付近の交差があるため、物理OFFで額に接している21枚の三角形IDを除き、新たに額へ入った三角形を数える。継続Runの20姿勢では、以前のHair設定905→現在12（約98.7%減）。この比較は同じ連続位相・方向0度で測定したもの。
- 現在の4種類の切り替え後40姿勢では追加交差の合計27、1姿勢あたり最大2枚。以前からの生え際の重なりも、微小な追加交差も完全には消していない。Bone位置／回転とSkinning面の差を持つボーン方式の制約であり、Particle/Sampleの侵入量がゼロに近いことと、表示三角形の非貫通は別の条件。

`motion-contact-metrics.txt`、`motion-model*-contacts.csv`、`motion-model*-frame*-view*.png`と`*-hair.png`、`anby-hair-bind.png`、`anby-transition-hair-worst.png`へ結果を保存する。回帰テストは位相の飛び、emaの胴体交差、アンビーの額への大きな折れ込み、旧JSONの既定値を検査する。Solver単体では等速移動時の追従、加速時の慣性、固定点を保つ角度制限、アニメーション方向の変更、反平行方向の有限な補正も検査する。

今回のDebug CPU更新平均はアンビー2.13ms（Cloth 1.77ms）、ema 2.66ms（Cloth 2.25ms）。`performance-motion-fix.csv`へ保存した。単発測定であり、読み込み・GPU描画・Present待ちを含まない。Debug／Releaseビルドは警告・エラー0。両モデルのDebug GPU実行テスト、Cloth Solver、既存Showroomテストが成功した。

## 制約と次の改善

脚とスカートの**ボーンParticle間**の接触は確認済み。ただし頂点や三角形を直接衝突させていないため、Particle間の面や厚い装飾には部分的な貫通が残り得る。自己衝突、連続衝突判定、布の厚み、布同士の接触も未対応。極端な瞬間回転や高速な動作は試作範囲を超える。

次はモデルに合わせたCollider形状／半径／オフセットの調整、実際の歩行クリップ、脚IKやPMX付与親の扱いが候補になる。Colliderの被覆を調整しても面の途中に貫通が残る場合は、ボーン密度・Skinningウェイト・三角形内部の検査や頂点サンプルを検討する。長いマントやボーンのない服には頂点ParticleとMesh／Material範囲指定、密な髪の枝分かれには共有固定点や専用の曲げ・角度Constraintが必要になる。今回は外部の大型物理ライブラリを追加していない。
