package io.github.bittahbandit1.refare;

import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.*;

/** Compiles only the imported user's executable; the APK supplies generic tools. */
final class GameCompiler {
 interface Progress {void update(String message);}
 private final File tools,nativeDir,cache,log;
 private final String toolchainId;
 private volatile boolean canceled;
 private final Set<Process> processes=ConcurrentHashMap.newKeySet();
 GameCompiler(File tools,File nativeDir,File cache,File log,String id){this.tools=tools;this.nativeDir=nativeDir;this.cache=cache;this.log=log;toolchainId=id;}
 void cancel(){canceled=true;for(Process process:processes)process.destroyForcibly();}
 private void check()throws IOException{if(canceled||Thread.currentThread().isInterrupted())throw new IOException("Game preparation canceled");}
 static String hash(File file)throws Exception {
  MessageDigest digest=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(file)){byte[] block=new byte[65536];int n;while((n=in.read(block))!=-1)digest.update(block,0,n);}
  StringBuilder value=new StringBuilder();for(byte b:digest.digest())value.append(String.format(Locale.ROOT,"%02x",b&255));return value.toString();
 }
 static void delete(File path)throws IOException {
  if(!path.exists())return;
  if(path.isDirectory()){File[] children=path.listFiles();if(children==null)throw new IOException("Cannot list "+path);for(File child:children)delete(child);}
  if(!path.delete())throw new IOException("Cannot remove temporary build file "+path);
 }
 static void install(InputStream input,File destination)throws Exception {
  File stage=new File(destination.getParentFile(),"compiler-install-"+UUID.randomUUID());
  if(!stage.mkdirs())throw new IOException("Cannot create compiler directory");
  try{
   long total=0;int files=0;
   try(ZipInputStream zip=new ZipInputStream(new BufferedInputStream(input))){ZipEntry entry;byte[] buffer=new byte[65536];
    while((entry=zip.getNextEntry())!=null){
     String name=entry.getName();if(name.startsWith("/")||name.contains("\\")||Arrays.asList(name.split("/")).contains(".."))throw new IOException("Invalid compiler archive path");
     if(++files>20000)throw new IOException("Too many compiler files");
     File output=new File(stage,name);if(entry.isDirectory()){output.mkdirs();continue;}output.getParentFile().mkdirs();
     try(OutputStream out=new FileOutputStream(output)){int n;while((n=zip.read(buffer))!=-1){total+=n;if(total>512L*1024*1024)throw new IOException("Compiler archive exceeds size limit");out.write(buffer,0,n);}}
    }
   }
   Files.write(new File(stage,"installed").toPath(),new byte[]{1});
   if(destination.exists())delete(destination);
   Files.move(stage.toPath(),destination.toPath(),StandardCopyOption.ATOMIC_MOVE);
  }finally{if(stage.exists())delete(stage);}
 }
 private void command(List<String> args)throws Exception {
  check();ProcessBuilder builder=new ProcessBuilder(args);builder.directory(tools);builder.redirectErrorStream(true);builder.redirectOutput(ProcessBuilder.Redirect.appendTo(log));
  Map<String,String> env=builder.environment();env.put("LD_LIBRARY_PATH",nativeDir.getAbsolutePath());env.put("TMPDIR",log.getParentFile().getAbsolutePath());env.put("LC_ALL","C");env.remove("LD_PRELOAD");
  Process process=builder.start();processes.add(process);
  try{while(!process.waitFor(200,TimeUnit.MILLISECONDS))check();check();if(process.exitValue()!=0)throw new IOException("Compiler command failed ("+process.exitValue()+"). Open the preparation log for details.");}
  finally{processes.remove(process);if(process.isAlive())process.destroyForcibly();}
 }
 private List<String> compileArgs(){
  List<String> args=new ArrayList<>(Arrays.asList(new File(nativeDir,"librefare-clang.so").getAbsolutePath(),"--driver-mode=gcc","--target=aarch64-linux-android26","--sysroot="+new File(tools,"sysroot"),"-resource-dir",new File(tools,"clang").getAbsolutePath(),"-fintegrated-cc1","-std=c11","-O2","-fPIC","-fvisibility=hidden","-ffunction-sections","-fdata-sections","-femulated-tls","-D_GNU_SOURCE","-DDOLRECOMP_CPU_HEADER=\"core/cpu.h\"","-DMODULE_GAME_ID=\"GCTE51\"","-I"+new File(tools,"module/GXRuntime/include"),"-I"+new File(tools,"module/abi")));
  return args;
 }
 private void compile(File source,File object,File generated,File stage)throws Exception {
  List<String> args=compileArgs();args.add("-I"+generated);args.add("-I"+stage);args.add("-c");args.add(source.getAbsolutePath());args.add("-o");args.add(object.getAbsolutePath());command(args);
 }
 File prepare(File dol,Progress progress)throws Exception {
  check();
  cache.mkdirs();log.getParentFile().mkdirs();Files.write(log.toPath(),("ReFare on-device compilation\nToolchain: "+toolchainId+"\n").getBytes("UTF-8"));
  String discHash=hash(dol);String key=discHash+"-"+toolchainId.substring(0,16);
  File destination=new File(cache,key),module=new File(destination,"module.so"),receipt=new File(destination,"ready.txt");
  if(module.isFile()&&receipt.isFile()){
   List<String> rows=Files.readAllLines(receipt.toPath());
   if(rows.size()==3&&rows.get(0).equals(discHash)&&rows.get(1).equals(toolchainId)&&rows.get(2).equals(hash(module))){check();progress.update("Using previously compiled game");return module;}
  }
  File stage=new File(cache,"build-"+UUID.randomUUID());if(!stage.mkdir())throw new IOException("Cannot create game build folder");
  ExecutorService workers=Executors.newFixedThreadPool(2);
  try{
   progress.update("Generating native code from your disc…");
   command(Arrays.asList(new File(nativeDir,"librefare-dolrecomp.so").getAbsolutePath(),"-j2","--backend=c","--cpu","gekko","--gamecube",dol.getAbsolutePath(),stage.getAbsolutePath()));
   File generated=new File(stage,"generated");Files.copy(dol.toPath(),new File(generated,"main.dol").toPath());ModuleTables.write(generated,new File(stage,"module_tables.inc"));
   File[] chunkFiles=new File(generated,"chunks").listFiles((dir,name)->name.endsWith(".c"));if(chunkFiles==null||chunkFiles.length==0)throw new IOException("No generated game chunks");Arrays.sort(chunkFiles);
   List<File> sources=new ArrayList<>();
   for(String name:new String[]{"cpu.c","cpu_exception.c","cpu_interpreter.c","cpu_interpreter_table.c","cpu_interpreter_float.c","cpu_interpreter_integer.c"})sources.add(new File(tools,"module/GXRuntime/src/core/"+name));
   sources.add(new File(tools,"module/module_export.c"));sources.addAll(Arrays.asList(chunkFiles));
   List<File> objects=new ArrayList<>();List<Future<?>> tasks=new ArrayList<>();AtomicInteger completed=new AtomicInteger();
   progress.update("Compiling game code: 0 / "+sources.size()+". First preparation may take several minutes.");
   for(int i=0;i<sources.size();i++){
    File source=sources.get(i),object=new File(stage,"object-"+i+".o");objects.add(object);
    tasks.add(workers.submit(()->{try{compile(source,object,generated,stage);progress.update("Compiling game code: "+completed.incrementAndGet()+" / "+sources.size());}catch(Exception error){throw new CompletionException(error);}}));
   }
   try{for(Future<?> task:tasks)task.get();}catch(ExecutionException error){cancel();Throwable cause=error.getCause();if(cause instanceof CompletionException&&cause.getCause()!=null)cause=cause.getCause();throw new IOException(cause.getMessage(),cause);}
   check();progress.update("Linking your native game module…");
   File linked=new File(stage,"module.so"),libs=new File(tools,"sysroot/usr/lib/aarch64-linux-android/26");
   List<String> args=new ArrayList<>(Arrays.asList(new File(nativeDir,"librefare-lld.so").getAbsolutePath(),"-flavor","gnu","-shared","-soname","refare_game.so","--no-undefined","--gc-sections","--strip-all","-z","relro","-z","now","--version-script="+new File(tools,"module/module.exports"),"-o",linked.getAbsolutePath(),new File(libs,"crtbegin_so.o").getAbsolutePath()));
   for(File object:objects)args.add(object.getAbsolutePath());
   args.add("-L"+libs);args.add("-lc");args.add("-lm");args.add("-ldl");args.add(new File(tools,"builtins.a").getAbsolutePath());args.add(new File(libs,"crtend_so.o").getAbsolutePath());command(args);check();
   if(!linked.setReadOnly())throw new IOException("Cannot protect the compiled module");
   Files.write(new File(stage,"ready.txt").toPath(),Arrays.asList(discHash,toolchainId,hash(linked)));
   // Keep only the finished module and receipt in the reusable cache.
   for(File child:Objects.requireNonNull(stage.listFiles()))if(!child.getName().equals("module.so")&&!child.getName().equals("ready.txt"))delete(child);
   check();if(destination.exists())delete(destination);Files.move(stage.toPath(),destination.toPath(),StandardCopyOption.ATOMIC_MOVE);
   progress.update("Game preparation complete");return module;
  }finally{workers.shutdownNow();for(Process process:processes)process.destroyForcibly();workers.awaitTermination(10,TimeUnit.SECONDS);if(stage.exists())delete(stage);}
 }
}
