//Java Launcher Win32/64

// version 1.5
// - supports passing command line options to java main()
// - loads CLASSPATH and MAINCLASS from PE-EXE resource
// - globbs arguments (see ExpandStringArray())
// - supports console apps (type "c")
// - supports windows services (type "s")

#include <windows.h>
#include <io.h>
#include <process.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <stddef.h>

#include <jni.h>

/* Global variables */
HKEY key, subkey;
int type;
char version[MAX_PATH];
char javahome[MAX_PATH];
char dll[MAX_PATH];
char crt[MAX_PATH];
int size = MAX_PATH;
HMODULE crt_dll;
HMODULE jvm_dll;
int (*CreateJavaVM)(void*,void*,void*);
HANDLE thread_handle;
int thread_id;
STARTUPINFO si;
PROCESS_INFORMATION pi;
char **g_argv;
int g_argc;
char module[MAX_PATH];
char exepath[MAX_PATH];
char classpath[1024];
char mainclass[MAX_PATH];
char method[MAX_PATH];
char xoptions[MAX_PATH];
#ifdef _JF_SERVICE
char service[MAX_PATH];
#endif
JavaVM *g_jvm = NULL;
JNIEnv *g_env = NULL;

/* Prototypes */
void error(char *msg);
int JavaStart(void *ignore);
int loadProperties();

/** Displays the error message in a dialog box. */
void error(char *msg) {
  char fullmsg[1024];
  sprintf(fullmsg, "Failed to start Java\nPlease visit www.java.com and install Java\nError(%d):%s", sizeof(void*) * 8, msg);
#ifndef _JF_SERVICE
  MessageBox(NULL, fullmsg, "Java Virtual Machine Launcher", (MB_OK | MB_ICONSTOP | MB_APPLMODAL));
#else
  printf("%s", fullmsg);
#endif
}

/** Converts array of C strings into array of Java strings */
jobjectArray
ConvertStringArray(JNIEnv *env, int strc, char **strv)
{
  jarray cls;
  jarray outArray;
  jstring str;
  int i;

  cls = (*env)->FindClass(env, "java/lang/String");
  outArray = (*env)->NewObjectArray(env, strc, cls, 0);
  for (i = 0; i < strc; i++) {
    str = (*env)->NewStringUTF(env, *strv++);
    (*env)->SetObjectArrayElement(env, outArray, i, str);
    (*env)->DeleteLocalRef(env, str);
  }
  return outArray;
}

/** Expands array of arguments (globbing)
 * Also releases inArray.
 */
jobjectArray
ExpandStringArray(JNIEnv *env, jobjectArray inArray) {
  jarray cls;
  jmethodID mid;
  jarray outArray;

  cls = (*env)->FindClass(env, "javaforce/JF");
  mid = (*env)->GetStaticMethodID(env, cls, "expandArgs", "([Ljava/lang/String;)[Ljava/lang/String;");
  outArray = (*env)->CallStaticObjectMethod(env, cls, mid, inArray);
  (*env)->DeleteLocalRef(env, inArray);
  return outArray;
}

char *DOption = "-Djava.class.path=";

/** Create class path adding exe path to each element (because the current path is not where the EXE is). */
char *CreateClassPath() {
  char *ClassPath;
  int sl = strlen(classpath);
  ClassPath = malloc(sl + 1);
  strcpy(ClassPath, classpath);
  int ml = strlen(exepath);
  char *jar[32];
  jar[0] = ClassPath;
  int cnt = 1;
  int a;
  for(a=0;a<sl;a++) {
    if (ClassPath[a] == ';') {
      jar[cnt++] = ClassPath + a + 1;
      ClassPath[a] = 0;
    }
  }
  int len = strlen(DOption) + sl + (ml * cnt) + 1;
  char *ExpandedClassPath = malloc(len);
  ExpandedClassPath[0] = 0;
  strcat(ExpandedClassPath, DOption);
  for(a=0;a<cnt;a++) {
    if (a > 0) strcat(ExpandedClassPath, ";");
    strcat(ExpandedClassPath, exepath);
    strcat(ExpandedClassPath, jar[a]);
  }
  return ExpandedClassPath;
}

void printException(JNIEnv *env) {
  jthrowable exc;
  exc = (*env)->ExceptionOccurred(env);
  if (exc == NULL) return;
  jclass newExcCls;
  (*env)->ExceptionDescribe(env);
  (*env)->ExceptionClear(env);
}

int InvokeMethod(char *_method, jobjectArray args, char *sign) {
  jclass cls = (*g_env)->FindClass(g_env, mainclass);
  if (cls == 0) {
    printException(g_env);
    error("Unable to find main class");
    return 0;
  }
  jmethodID mid = (*g_env)->GetStaticMethodID(g_env, cls, _method, sign);
  if (mid == 0) {
    printException(g_env);
    error("Unable to find main method");
    return 0;
  }

  (*g_env)->CallStaticVoidMethod(g_env, cls, mid, args);

  return 1;
}

JavaVMInitArgs *BuildArgs() {
  JavaVMInitArgs *args;
  JavaVMOption *options;
  int nOpts = 1;
  char *opts[64];
  int idx;

  opts[0] = CreateClassPath();
  if (strlen(xoptions) > 0) {
    char *x = xoptions;
    while (x != NULL) {
      opts[nOpts++] = x;
      x = strchr(x, ' ');
      if (x != NULL) {
        *x = 0;
        x++;
      }
    }
  }

  args = malloc(sizeof(JavaVMInitArgs));
  memset(args, 0, sizeof(JavaVMInitArgs));
  options = malloc(sizeof(JavaVMOption) * nOpts);
  memset(options, 0, sizeof(JavaVMOption) * nOpts);

  for(idx=0;idx<nOpts;idx++) {
    options[idx].optionString = opts[idx];
//    printf("[] = %s\n", opts[idx]);  //debug
  }

  args->version = JNI_VERSION_1_2;
  args->nOptions = nOpts;
  args->options = options;
  args->ignoreUnrecognized = JNI_FALSE;

  return args;
}

void FreeArgs(JavaVMInitArgs *args) {
  int idx;
  for(idx=0;idx<args->nOptions;idx++) {
    free(args->options[idx].optionString);
  }
  free(args->options);
  free(args);
}

/** Creates a new JVM. */
int CreateJVM() {
  JavaVMInitArgs *args = BuildArgs();
  if ((*CreateJavaVM)(&g_jvm, &g_env, args) == -1) {
    error("Unable to create Java VM");
    return 0;
  }

//  FreeArgs(args);

  return 1;
}

/** Attachs current thread to JVM. */
int AttachJVM() {
  (*g_jvm)->AttachCurrentThread(g_jvm, (void**)&g_env, NULL);
}

/** Invokes the main method in a new thread. */
int JavaStart(void *ignore) {
  CreateJVM();

  char **argv = g_argv;
  int argc = g_argc;
  //skip argv[0]
  argv++;
  argc--;
  InvokeMethod(method, ExpandStringArray(g_env, ConvertStringArray(g_env, argc, argv)), "([Ljava/lang/String;)V");

  (*g_jvm)->DestroyJavaVM(g_jvm);  //waits till all threads are complete

  return 1;
}

int loadProperties() {
  void *data;
  char *str, *ln1, *ln2;
  HRSRC res;
  HGLOBAL global;
  int size;

  xoptions[0] = 0;

  strcpy(method, "main");  //default method name
  javahome[0] = 0;  //detect later

  res = FindResource(NULL, MAKEINTRESOURCE(1), RT_RCDATA);
  if (res == NULL) {error("Unable to FindResource"); return 0;}
  size = SizeofResource(NULL, res);
  global = LoadResource(NULL, res);
  if (global == NULL) {error("Unable to LoadResource"); return 0;}
  data = LockResource(global);
  if (data == NULL) {error("Unable to LockResource"); return 0;}
  str = malloc(size+1);
  memcpy(str, data, size);
  str[size] = 0;  //NULL terminate
  FreeResource(global);

  ln1 = str;
  classpath[0] = 0;
  mainclass[0] = 0;
  while (ln1 != NULL) {
    ln2 = strstr(ln1, "\r\n");
    if (ln2 != NULL) {
      *ln2 = 0;
      ln2++;
//      *ln2 = 0;
      ln2++;
    } else {
      ln2 = strchr(ln1, '\n');
      if (ln2 != NULL) {
        *ln2 = 0;
        ln2++;
      }
    }
    if (strncmp(ln1, "CLASSPATH=", 10) == 0) {
      strcpy(classpath, ln1 + 10);
    }
    else if (strncmp(ln1, "MAINCLASS=", 10) == 0) {
      strcpy(mainclass, ln1 + 10);
    }
    else if (strncmp(ln1, "JAVA_HOME=", 10) == 0) {
      strcpy(javahome, ln1 + 10);
    }
    else if (strncmp(ln1, "OPTIONS=", 8) == 0) {
      strcpy(xoptions, ln1 + 8);
    }
#ifdef _JF_SERVICE
    else if (strncmp(ln1, "SERVICE=", 8) == 0) {
      strcpy(service, ln1 + 8);
    }
#else
    else if (strncmp(ln1, "METHOD=", 7) == 0) {
      strcpy(method, ln1 + 7);
    }
#endif
    ln1 = ln2;
  }
  free(str);
  return 1;
}

int exists(char *file) {
  if (GetFileAttributes(file) == INVALID_FILE_ATTRIBUTES) return 0;
  return 1;
}

int findJavaHomeAppFolder() {
  //try to find JRE in Apps folder
  strcpy(javahome, exepath);
  strcat(javahome, "jre\\");
  int sl = strlen(javahome);
  strcat(javahome, "bin\\server\\jvm.dll");
  if (exists(javahome) == 1) {
    javahome[sl] = 0;
    return 1;
  }
  javahome[sl] = 0;
  strcat(javahome, "bin\\client\\jvm.dll");
  if (exists(javahome) == 1) {
    javahome[sl] = 0;
    return 1;
  }
  return 0;
}

int findJavaHomeAppDataFolder() {
  //try to find JRE in %AppData% folder
  GetEnvironmentVariable("APPDATA", javahome, MAX_PATH);
  strcat(javahome, "\\java\\jre");
  if (sizeof(void*) == 4)
    strcat(javahome, "32");
  else
    strcat(javahome, "64");
  int sl = strlen(javahome);
  strcat(javahome, "\\bin\\server\\jvm.dll");
  if (exists(javahome) == 1) {
    javahome[sl] = 0;
    return 1;
  }
  javahome[sl] = 0;
  strcat(javahome, "\\bin\\client\\jvm.dll");
  if (exists(javahome) == 1) {
    javahome[sl] = 0;
    return 1;
  }
  return 0;
}

int findJavaHomeRegistry() {
  //try to find JRE in Registry
  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\JavaSoft\\Java Runtime Environment", 0, KEY_READ, &key) != 0) {
    return 0;
  }

  size = 0;
  if (RegQueryValueEx(key, "CurrentVersion", 0, (LPDWORD)&type, 0, (LPDWORD)&size) != 0 || (type != REG_SZ) || (size > MAX_PATH)) {
    return 0;
  }

  size = MAX_PATH;
  if (RegQueryValueEx(key, "CurrentVersion", 0, 0, version, (LPDWORD)&size) != 0) {
    return 0;
  }

  if (RegOpenKeyEx(key, version, 0, KEY_READ, &subkey) != 0) {
    return 0;
  }

  size = 0;
  if (RegQueryValueEx(subkey, "JavaHome", 0, (LPDWORD)&type, 0, (LPDWORD)&size) != 0 || (type != REG_SZ) || (size > MAX_PATH)) {
    return 0;
  }

  size = MAX_PATH;
  if (RegQueryValueEx(subkey, "JavaHome", 0, 0, javahome, (LPDWORD)&size) != 0) {
    return 0;
  }

  RegCloseKey(key);
  RegCloseKey(subkey);
  return 1;
}

#ifdef _JF_SERVICE

SERVICE_STATUS_HANDLE ServiceHandle;
int s_argc;
char **s_argv;

void ServiceStatus(int state) {
  SERVICE_STATUS ss;

  ss.dwServiceType = SERVICE_WIN32;
  ss.dwWin32ExitCode = 0;
  ss.dwCurrentState = state;
  ss.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  ss.dwWin32ExitCode = 0;
  ss.dwServiceSpecificExitCode = 0;
  ss.dwCheckPoint = 0;
  ss.dwWaitHint = 0;

  SetServiceStatus(ServiceHandle, &ss);
}

void __stdcall ServiceControl(int OpCode) {
  switch (OpCode) {
    case SERVICE_CONTROL_STOP:
      AttachJVM();
      ServiceStatus(SERVICE_STOPPED);
      InvokeMethod("serviceStop", NULL, "()V");
      break;
  }
}

void __stdcall ServiceMain(int argc, char **argv) {
  ServiceHandle = RegisterServiceCtrlHandler(service, (void (__stdcall *)(unsigned long))ServiceControl);
  ServiceStatus(SERVICE_RUNNING);
  CreateJVM();
  InvokeMethod("serviceStart", ConvertStringArray(g_env, argc, argv), "([Ljava/lang/String;)V");
  (*g_jvm)->DestroyJavaVM(g_jvm);
}

#endif

/** Main entry point. */
int main(int argc, char **argv) {
  g_argv = argv;
  g_argc = argc;
  //TODO : use GetCommandLine() instead to preserve "quotes"

  GetModuleFileName(NULL, module, MAX_PATH);
  strcpy(exepath, module);
  char *LastPath = strrchr(exepath, '\\');
  LastPath++;
  *LastPath = 0;

  if (loadProperties() == 0) {
    error("Unable to load properties");
    return 2;
  }

  if (javahome[0] == 0) {
    if (findJavaHomeAppFolder() == 0) {
      if (findJavaHomeRegistry() == 0) {
        if (findJavaHomeAppDataFolder() == 0) {
          error("Unable to find Java");
        }
      }
    }
  }

  //JRE7/8
  strcpy(crt, javahome);
  strcat(crt, "\\bin\\msvcr100.dll");
  if ((crt_dll = LoadLibrary(crt)) == 0) {
    //older JRE5/6 version
    strcpy(crt, javahome);
    strcat(crt, "\\bin\\msvcr71.dll");
    if ((crt_dll = LoadLibrary(crt)) == 0) {
      //could be a much older version (JRE5???) which just uses msvcrt.dll
    }
  }

  strcpy(dll, javahome);
  strcat(dll, "\\bin\\server\\jvm.dll");
  if ((jvm_dll = LoadLibrary(dll)) == 0) {
    strcpy(dll, javahome);
    strcat(dll, "\\bin\\client\\jvm.dll");
    if ((jvm_dll = LoadLibrary(dll)) == 0) {
      error("Unable to open jvm.dll");
      return 2;
    }
  }

  CreateJavaVM = (int (*)(void*,void*,void*)) GetProcAddress(jvm_dll, "JNI_CreateJavaVM");
  if (CreateJavaVM == NULL) {
    error("Unable to find Java interfaces in jvm.dll");
    return 2;
  }

#ifdef _JF_SERVICE
  void *ServiceTable[4];
  ServiceTable[0] = (void*)service;
  ServiceTable[1] = (void*)ServiceMain;
  ServiceTable[2] = NULL;
  ServiceTable[3] = NULL;
  StartServiceCtrlDispatcher((LPSERVICE_TABLE_ENTRY)&ServiceTable);  //does not return until all services have been stopped
#else
  thread_handle = CreateThread(NULL, 64 * 1024, (LPTHREAD_START_ROUTINE)&JavaStart, NULL, 0, (LPDWORD)&thread_id);
  WaitForSingleObject(thread_handle, INFINITE);
#endif

  return 0;
}
