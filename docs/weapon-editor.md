# Weapon Editor

Debugビルドの **Weapon System → Weapon Editor** から開きます。FPS操作中はESCでマウスを解放してください。Releaseビルドには編集UIを出しません。

## 操作

Ammoタブの **Infinite Reserve (reload required)** は予備弾だけを無限にします。装弾数・射撃間隔・リロード時間は維持します。現在のSub武器はONです。

1. 左ペインで武器を検索・選択します。New WeaponはIDとDisplay Nameを入力して作成、Duplicateは選択武器の性能をコピーします。作成・削除を含む変更はSaveまで下書きです。
2. 中央（ショウルームでは右）の「Editing: ID / 名前」が左の選択武器と一致していることを確認し、General / Fire / Ammo / Reload / ADS・Accuracyで編集します。武器を選び直すと編集対象も切り替わり、開いているタブは維持されます。Typeは自由な文字列です。BurstおよびPerRoundの専用項目は対応モードのときだけ表示します。
3. 右ペインでRPM（60 / fireInterval）、Pellet数、PelletあたりDamage、全Pellet命中時のDamage、Magazine内の発射回数、初期総弾数を確認します。Burstの内部間隔はRPMとは別表示です。
4. **Save & Equip** を押し、Sceneをクリックして射撃します。弾数・Reload・Burst・Cooldownは既存のDebug Equipと同じ経路で初期化します。ESCで再びEditorへ戻って調整できます。

**Save** は全下書きを保存し、ゲームの定義を更新します。現在装備中の性能・弾数は通常そのままなので、性能を試すにはSave & Equipを使ってください。装備中の武器を削除またはID変更した場合はInitial Weaponへ装備を戻します。

**Revert** は確認後に全下書きを破棄し、ディスクのweapons.jsonを読み直します。ウィンドウを閉じるだけなら同じScene内で下書きを保持します。Scene変更・終了前にはSaveしてください。

## IDと削除

新規・変更IDは英字で始まる1～64文字の英数字・`_`・`-`です。重複IDは保存できません。Display Nameは処理用IDと別で、現在のローダーに合わせて1～48 UTF-8バイトです。

Deleteは確認Popupを表示します。削除・ID変更はStageや.blend側の参照を書き換えません。現在のStageのPool／Filterが不正になる変更や、現在配置されているPickupのID削除は保存を拒否します。別Stage・.blendの参照は自動検索しないため、変更後はそれぞれの参照を確認してください。

Initial Weaponは設定欄の「Game Start Settings」を開いて選択できます。これはゲーム開始時の装備の設定で、編集対象は左の武器リストで選択します。初期武器を削除する場合は、先に別の武器を指定してください。現在Stageから参照されている武器は、Stage側を修正して再起動してから削除します。

## 保存と互換性

編集値の項目別検証後、一時JSONを既存WeaponSystemで読み込み、現在のStageも検証します。成功したファイルだけを置換し、`weapons.json.debug-backup` を残します。外部でJSONが更新された場合は上書きせず、Revertで再読込するまで保存を拒否します。

未知のJSONフィールド・未編集武器の省略フィールドを保持します。旧JSONの省略可能フィールドを必須化していません。依存する省略値（maxReserveAmmo、adsSpreadDegrees）は、別項目の編集で意図せず変化しないよう必要時のみ明示保存します。

保存時にPickupの抽選や復活は行わず、取得済み状態と配置済み武器IDを保持します。Cubeの色とサイズは更新します。Spawn Filterによる新しい抽選結果はStage再起動で確認してください。旧定義への巻き戻りを防ぐため、保存成功時はDebug Timeline履歴をクリアします。

専用モデル・アニメーション・音・Muzzle Flash・Recoil編集は含みません。右ペインのPreviewは将来用の表示枠です。

## 検証

- `tools/build.ps1 -Configuration Debug`
- `tools/build.ps1 -Configuration Release`
- `tools/test-weapons.ps1`：射撃、弾数、Burst、Reload、Pickup、Spawn Filter、旧JSONなど
- `tools/test-weapon-editor.ps1`：作成・複製、保存と検証、外部更新／I/O失敗、既存JSON保持、削除安全性、装備初期化、ImGui描画

テストはgenerated配下のコピーを使用し、本番のweapons.jsonは変更しません。

Game Start SettingsのInitial Weaponで **None (unarmed)** を選ぶと、開始時は武器を持ちません。保存値は `initialWeapon: ""`、現在の初期設定はこれです。床の配置武器は通常どおりEで拾えます。武器IDを指定した場合は、その武器を初期装備します。
