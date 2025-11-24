import os
import hashlib
from collections import defaultdict

def find_duplicate_files_by_prefix(folder_path, prefix):
    """
    在指定的資料夾路徑中，尋找檔名以特定前綴開頭且內容完全相同的檔案。

    Args:
        folder_path (str): 要掃描的資料夾路徑。
        prefix (str): 檔案名稱必須以此字串開頭。

    Returns:
        list: 一個包含多個列表的列表，每個子列表都包含內容相同的檔案路徑。
              如果沒有重複的檔案，則返回一個空列表。
    """
    if not os.path.isdir(folder_path):
        print(f"錯誤：提供的路徑 '{folder_path}' 不是一個有效的資料夾。")
        return []

    # key: 檔案內容的 hash, value: 擁有相同 hash 的檔案路徑列表
    hashes = defaultdict(list)

    # 遍歷資料夾中的所有檔案
    for filename in os.listdir(folder_path):
        # --- 主要修改處 ---
        # 檢查檔案名稱是否以指定的前綴開頭
        if filename.startswith(prefix):
            print(f"檢查檔案: {filename}")
            file_path = os.path.join(folder_path, filename)
            
            # 確保這是檔案而不是子資料夾
            if os.path.isfile(file_path):
                try:
                    # 使用二進制模式 'rb' 讀取檔案，以確保 hash 的一致性
                    with open(file_path, 'rb') as f:
                        file_content = f.read()
                        # 計算檔案內容的 SHA-256 hash
                        file_hash = hashlib.sha256(file_content).hexdigest()
                        # 將檔案路徑加入對應 hash 的列表中
                        hashes[file_hash].append(file_path)
                except IOError as e:
                    print(f"無法讀取檔案 {file_path}: {e}")

    # 找出所有檔案路徑列表長度大於 1 的項目，這些就是重複的檔案
    duplicate_groups = [paths for paths in hashes.values() if len(paths) > 1]
    
    return duplicate_groups

if __name__ == "__main__":
    # --- 請修改以下設定 ---

    # 1. 設定要檢測的資料夾路徑
    #    '.' 代表當前 script 所在的資料夾
    target_folder = '.'  
    
    # 2. 設定要檢查的檔案名前綴 (開頭的文字)
    file_prefix = 'output' 
    
    # --- 設定結束 ---

    print(f"正在掃描資料夾: '{os.path.abspath(target_folder)}'")
    print(f"尋找檔名以 '{file_prefix}' 開頭的檔案...\n")
    
    duplicate_files = find_duplicate_files_by_prefix(target_folder, file_prefix)

    if not duplicate_files:
        print(f"結果：在此資料夾中，所有以 '{file_prefix}' 開頭的檔案內容都不相同，或只有一個這樣的檔案。")
    else:
        print(f"結果：共找到 {len(duplicate_files)} 組內容相同的檔案 (檔名以 '{file_prefix}' 開頭)。\n")
        for i, group in enumerate(duplicate_files, 1):
            print(f"--- 第 {i} 組 (內容相同) ---")
            for file_path in group:
                print(f"  - {file_path}")
            print("-" * (12 + len(str(i)))) # 分隔線