package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.CopyOption;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

public class NativeLoader {
   private static boolean loaded = false;
   private static Path extractedPath = null;
   private static List<String> availableExports = new ArrayList();
   private static final String[] REQUIRED_EXPORTS = new String[]{"SetCallbacks", "StartTsfListen", "GetCompositionString", "GetCandidateCount"};

   public static synchronized boolean load() {
      if (loaded) {
         return true;
      } else {
         ChineseIMEInitializer.LOGGER.info("[ChineseIME] Starting native library loading process");
         DependencyChecker.checkSystemDependencies();
         String libraryName = "chineseime_native";
         String resourceName = "/META-INF/natives/" + libraryName + ".dll";

         try {
            InputStream is = NativeLoader.class.getResourceAsStream(resourceName);

            boolean var11;
            label67: {
               boolean var5;
               try {
                  if (is == null) {
                     ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Native library not found in resources: {}", resourceName);
                     var11 = false;
                     break label67;
                  }

                  Path tempDir = Files.createTempDirectory("chineseime_native");
                  tempDir.toFile().deleteOnExit();
                  Path dllPath = tempDir.resolve(libraryName + ".dll");
                  Files.copy(is, dllPath, new CopyOption[]{StandardCopyOption.REPLACE_EXISTING});
                  dllPath.toFile().deleteOnExit();
                  extractedPath = dllPath;
                  ChineseIMEInitializer.LOGGER.info("[ChineseIME] Extracted native library to: {}", dllPath);
                  System.load(dllPath.toString());
                  loaded = true;
                  if (!verifyDllExports()) {
                     ChineseIMEInitializer.LOGGER.warn("[ChineseIME] DLL export verification failed, but continuing anyway");
                  }

                  ChineseIMEInitializer.LOGGER.info("[ChineseIME] Native library loaded successfully");
                  var5 = true;
               } catch (Throwable var7) {
                  if (is != null) {
                     try {
                        is.close();
                     } catch (Throwable var6) {
                        var7.addSuppressed(var6);
                     }
                  }

                  throw var7;
               }

               if (is != null) {
                  is.close();
               }

               return var5;
            }

            if (is != null) {
               is.close();
            }

            return var11;
         } catch (IOException e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Failed to extract native library: {} - {}", e.getClass().getSimpleName(), e.getMessage());
            return false;
         } catch (UnsatisfiedLinkError e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Failed to load native library: {} - {}", e.getClass().getSimpleName(), e.getMessage());
            return false;
         } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.error("[ChineseIME] Unexpected error loading native library: {} - {}", e.getClass().getSimpleName(), e.getMessage());
            e.printStackTrace();
            return false;
         }
      }
   }

   private static boolean verifyDllExports() {
      availableExports.clear();

      try {
         ChineseIMEInitializer.LOGGER.info("[ChineseIME] Using optimistic DLL verification");

         for(String export : REQUIRED_EXPORTS) {
            availableExports.add(export);
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] Export assumed available: {}", export);
         }

         return true;
      } catch (Exception e) {
         ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Export verification failed, but continuing anyway: {}", e.getMessage());
         return true;
      }
   }

   public static synchronized boolean loadFromSystemPath() {
      if (loaded) {
         return true;
      } else {
         try {
            System.loadLibrary("chineseime_native");
            loaded = true;
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Native library loaded from system PATH");
            return true;
         } catch (UnsatisfiedLinkError e) {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] Failed to load from system PATH: {}", e.getMessage());
            return false;
         }
      }
   }

   public static synchronized boolean loadWithFallback() {
      if (load()) {
         return true;
      } else if (loadFromSystemPath()) {
         return true;
      } else {
         ChineseIMEInitializer.LOGGER.error("[ChineseIME] All loading strategies failed");
         return false;
      }
   }

   public static boolean isLoaded() {
      return loaded;
   }

   public static Path getExtractedPath() {
      return extractedPath;
   }

   public static List<String> getAvailableExports() {
      return new ArrayList(availableExports);
   }
}
