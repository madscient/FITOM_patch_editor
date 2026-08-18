# spec/ — FITOM_X からの同期コピー

`chip-capabilities.json` は **FITOM_X 本体の `spec/chip-capabilities.json`
をそのままコピーしたもの**です。このリポジトリで編集しないでください。

チップ種別(`VoicePatchType`)ごとに、HwPatch のどのフィールドが実際に
意味を持ち、値域・実効解像度・有効条件がどうなるかを定義しています。
本エディタはこれを唯一の情報源として、パッチ編集画面のスライダ範囲・
未使用フィールドの非表示・保存時のフィールド省略を決めています
(`fpe::ChipCapabilities`、`docs/DESIGN.md` D-057)。

## 実行時の探索

`fpe::ChipCapabilities::findSpecFile()` が、カレントディレクトリ →
実行ファイルのあるディレクトリの順に、上位方向へ `spec/chip-capabilities.json`
を探します(`assets/` や `fixtures/` と同じ方式)。GUI のビルド時には
実行ファイルの隣にもコピーされます。見つからない場合、エディタは
全フィールドを暫定表示にしたうえで警告を出します。

## 更新手順

FITOM_X 側が更新されたら、

1. `cp ../FITOM_X/spec/chip-capabilities.json spec/`
2. `ctest --test-dir build/<preset> -C Debug`

スモークテストは代表的なチップの値域・有効条件・オペレータ数を実際に
検証しているので、意図しない変更があればここで落ちます。落ちた場合は、
それが FITOM_X 側の意図した変更なのかを確認してからテストを更新して
ください。
