## 変更内容
- `Group` に Sort 機能を追加
- ソート対象に Owner 優先度や実行順制御を反映
- 今後 `Signal` クラスに流用するための準備

## メモ
- Signal 側に dispatchFront, 優先度, policy の流用方針あり
- Group 系のユニットテストは別PR予定

## 次の予定
- `Signal` への Sort 移植
- オーナー優先グループの完成
- 即時 dispatch 対応