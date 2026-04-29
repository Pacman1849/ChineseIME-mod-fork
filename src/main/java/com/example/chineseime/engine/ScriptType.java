package com.example.chineseime.engine;

public enum ScriptType {
   SIMPLIFIED("简体", "Simplified"),
   TRADITIONAL("繁体", "Traditional");

   private final String chineseName;
   private final String englishName;

   private ScriptType(String chineseName, String englishName) {
      this.chineseName = chineseName;
      this.englishName = englishName;
   }

   public String getChineseName() {
      return this.chineseName;
   }

   public String getEnglishName() {
      return this.englishName;
   }

   // $FF: synthetic method
   private static ScriptType[] $values() {
      return new ScriptType[]{SIMPLIFIED, TRADITIONAL};
   }
}
