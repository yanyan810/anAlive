# Enemy Editor

## モデルを選んで部位を作る

1. Debug／Developmentのタイトルで **F3** を押し、ShowroomのWeapon Editor上部にある **Open Enemy Editor** を押します。**Back to Weapon Editor** で武器の編集画面に戻ります。
2. **Model** から `resources` 以下のモデルを選びます。**Filter models** はファイル名、相対パス、拡張子で絞り込めます。追加したファイルは **Rescan resources** で表示します。PMX・glTF・GLB・OBJ・FBX・PMDが対象です。
3. 右ドラッグで回転、中ドラッグでパン、ホイールでズームします。モデルは自動的にフレームに収まります。**Model scale** は保存される表示・命中判定の倍率です。倍率を変えた後に **Frame model** で再度フレームに収められます。
4. **Add Part** で部位を作り、名前とRoleを設定します。**Select** は面を選択に追加し、**Deselect** は選択から外します。左クリックは手前の三角形、左ドラッグは画面上の矩形に重心が入る三角形が対象です。矩形は奥の面も含みます。全解除は **Clear selection** を使います。
5. 初期状態では **Lock assigned faces** が有効です。新しく選べるのは未割り当て面で、ロック中に **Assign selection** が完了すると選択をクリアします。ロックをオンに切り替えると、選択中の割り当て済み面が選択から外れます。**Select Part faces** ではロック中も既存Partを確認できます。再割り当てや **Unassign selection** を行うにはロックを解除します。**Part colors**、**Show unassigned**、**Isolate Part**、**Dim other Parts** で複雑なモデルを確認できます。
6. **HP mode** でLocal／Shared／LocalAndShared／Invincibleを選びます。Sharedを使うときは **Shared HP groups** にグループを追加し、部位から選びます。Local HPが0になると、Breakableの部位が外れます。部位または共有グループのDeath on Zeroが有効なら、該当HPが0になった時点で敵が死亡します。Shared Damage Rateは共有HPへのダメージ倍率です。割り当てのない面には個別HPがなく、被弾してもその面だけは破壊されません。
7. `resources/enemy/custom/名前.enemy.json` などを指定して **Save** します。**Save As** では同じモデルから別設定を作成できます。**Load JSON** は入力欄のファイルを開き、**Reload** は作業中のファイルをディスクから読み直し、**Discard edits** は最後に開いたか保存した設定に戻します。保存・試射する前に、作成した各Partに面を割り当ててください。

## Showroomで試射する

- **Test this draft in Showroom** は編集内容をメモリ上で検証し、Normal Testに適用します。未装備の場合はWeapon Editorで選択している武器が優先されます。最初のPartに照準を合わせた位置で始まります。ビューをクリックして照準を操作し、左クリックで射撃します。**Esc** で同じ編集内容に戻り、**F5** で最後に選んだ保存済みのAssetを復元します。
- 保存済みAssetはShowroomの **Normal Test (.enemy.json)** から選べます。試射中は **Tab** で同じパネルを表示します。外部ツールで上書きした場合は **Reload JSON** を押します。
- 起動時の指定は `resources/Data/showroom_enemy_test.json` の `enemyAsset` で行います。**Reload Test Config** で入場中にも読み直せます。`enemyDefinition` は行動・移動用の敵定義IDです。既定値は `normal` です。
- Assetの変更ではNormal Testの外観・命中面・HP・破壊状態をセットで交換します。読み込みや検証に失敗した場合は、直前の有効なAssetを使い続けます。既存の展示個体は別々のプール枠です。

## ファイル形式と制限

- version 2の `model` には `resources` から始まるパス、形状署名、面数、倍率があります。各面は `[instance, mesh, material, triangle]` で識別します。形状署名はエンジンのインポーターが確定した頂点・インデックス構成とノード変換から作ります。元モデルや外部バッファを更新して署名が変わった場合は、モデルを選び直して割り当ててください。
- モデルの最長軸を基準に大きさを揃え、床面に置きます。PMXとスキニング付きglTFはインポート時の静止形状で扱います。部位を外すときも元のマテリアルを使います。ボーンアニメーション中の追従・切断や、PMX固有の特殊シェーダーは対象外です。入力に有効な三角形がない場合などはモデル選択時にエラーを表示します。上限は100万三角形・4096部位です。
- version 1の三角形埋め込み形式はShowroomと従来の敵で読み込めます。version 1の編集にはBlenderアドオンを使います。
- ShowroomとEnemy EditorはDebug／Development用です。Release x64には従来の製品版シーン構成が適用されます。

## Blenderで編集する

- **Engine → Blender**。Enemy Editorで保存後、**Blender mesh** を押します。出力された `名前.enemy.json.mesh.json` を `名前.enemy.json` の隣に置き、Blenderの **Enemy Parts → Import Enemy Asset** で `名前.enemy.json` を開きます。メッシュは新しいコレクションに作られます。元モデルのインポート順とBlender側の面順が異なる場合でも、エンジンの面IDをそのまま使えます。
- **Blender → Engine**。作業用メッシュを複数選択して編集モードに入り、必要な面を選んで **Assign Selected Faces** を使います。**Export Enemy Asset** はコレクション内の面IDを使ってversion 2を出力します。頂点位置、オブジェクト変換、面構成を変更すると出力時に検証エラーになります。Blender独自の形状で作りたい場合は、通常のBlenderメッシュからversion 1スナップショットを出力できます。
- **Blender独自のモデル**。対象のMesh Objectをすべて選択し、PartとGroupを定義したMesh Objectをアクティブにします。面の割り当ては選択中のメッシュ群に適用されます。version 1のテクスチャはアクティブメッシュで指定した1枚です。version 1のJSONも直接インポートできます。
- `resources/enemy/boss/normal-parts.blend` は重複座標を共有し、部位の境界をまたいで面選択できる編集用メッシュです。頂点は2,136から362に減り、712面すべての座標・向き・部位割り当てを保持しました。辺でつながる島は2つです。Headの192面が独立しており、残りの520面はHeadの10面と胴体・四肢で構成されています。
