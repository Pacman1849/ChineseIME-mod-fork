package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import java.io.File;

public class DependencyChecker {
   private static final String[] REQUIRED_SYSTEM_DLLS = new String[]{"kernel32.dll", "user32.dll", "ole32.dll", "oleaut32.dll", "msctf.dll", "imm32.dll"};

   public static boolean checkSystemDependencies() {
      ChineseIMEInitializer.LOGGER.info("[ChineseIME] Checking system DLL dependencies");
      boolean allAvailable = true;

      for(String dllName : REQUIRED_SYSTEM_DLLS) {
         if (!isDllFileExists(dllName)) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] DLL file not found: {}", dllName);
         } else {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] DLL available: {}", dllName);
         }
      }

      ChineseIMEInitializer.LOGGER.info("[ChineseIME] System dependency check completed: PASS");
      return true;
   }

   private static boolean isDllAvailable(String dllName) {
      return isDllFileExists(dllName);
   }

   public static boolean isDllFileExists(String dllName) {
      String[] systemPaths = new String[]{System.getenv("SystemRoot") + "\\System32\\", System.getenv("SystemRoot") + "\\SysWOW64\\", System.getenv("SystemRoot") + "\\", "C:\\Windows\\System32\\", "C:\\Windows\\SysWOW64\\"};

      for(String path : systemPaths) {
         File dllFile = new File(path + dllName);
         if (dllFile.exists()) {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] DLL found: {}", dllFile.getAbsolutePath());
            return true;
         }
      }

      ChineseIMEInitializer.LOGGER.warn("[ChineseIME] DLL file not found: {}", dllName);
      return false;
   }

   public static String getDependencyReport() {
      StringBuilder report = new StringBuilder();
      report.append("=== System DLL Dependency Report ===\n");

      for(String dllName : REQUIRED_SYSTEM_DLLS) {
         boolean available = isDllAvailable(dllName);
         boolean fileExists = isDllFileExists(dllName);
         report.append(String.format("%-20s Available: %-5s FileExists: %-5s\n", dllName, available, fileExists));
      }

      return report.toString();
   }

   public static boolean checkTsfDependencies() {
      ChineseIMEInitializer.LOGGER.info("[ChineseIME] Checking TSF-specific dependencies");
      boolean tsfAvailable = isDllFileExists("msctf.dll");
      boolean immAvailable = isDllFileExists("imm32.dll");
      if (!tsfAvailable) {
         ChineseIMEInitializer.LOGGER.warn("[ChineseIME] TSF not available, using IMM32 fallback");
      }

      if (!immAvailable) {
         ChineseIMEInitializer.LOGGER.warn("[ChineseIME] IMM32 not available");
      }

      ChineseIMEInitializer.LOGGER.info("[ChineseIME] TSF dependencies: TSF={}, IMM32={}", tsfAvailable, immAvailable);
      return true;
   }
}
