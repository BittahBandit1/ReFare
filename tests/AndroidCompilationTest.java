package io.github.bittahbandit1.refare;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
public final class AndroidCompilationTest {
 public static void main(String[] args)throws Exception {
  if(args[0].equals("tables")){ModuleTables.write(new File(args[1]),new File(args[2]));return;}
  File root=new File(args[1]);root.mkdirs();File dol=new File(root,"main.dol");Files.write(dol.toPath(),new byte[]{1,2,3,4});
  String id="0123456789abcdef0123456789abcdef",digest=GameCompiler.hash(dol);File cache=new File(root,"modules");File entry=new File(cache,digest+"-"+id.substring(0,16));entry.mkdirs();File module=new File(entry,"module.so");Files.write(module.toPath(),new byte[]{5,6,7});Files.write(new File(entry,"ready.txt").toPath(),Arrays.asList(digest,id,GameCompiler.hash(module)));
  File log=new File(root,"logs/preparation.log");GameCompiler compiler=new GameCompiler(new File(root,"absent-tools"),new File(root,"absent-binaries"),cache,log,id);
  assert compiler.prepare(dol,message->{}).equals(module):"reuse verified cache without invoking a compiler";
  GameCompiler canceled=new GameCompiler(root,root,cache,log,id);canceled.cancel();
  try{canceled.prepare(dol,message->{});throw new AssertionError("canceled cache reuse");}catch(IOException expected){assert expected.getMessage().contains("canceled");}
  Files.write(module.toPath(),new byte[]{9});
  try{compiler.prepare(dol,message->{});throw new AssertionError("accepted corrupt cached module");}catch(IOException expected){}
  assert Objects.requireNonNull(cache.listFiles((dir,name)->name.startsWith("build-"))).length==0:"failed build cleanup";
  File installed=new File(root,"installed/compiler");ByteArrayOutputStream buffer=new ByteArrayOutputStream();try(ZipOutputStream zip=new ZipOutputStream(buffer)){zip.putNextEntry(new ZipEntry("headers/test.h"));zip.write(new byte[]{42});zip.closeEntry();}
  GameCompiler.install(new ByteArrayInputStream(buffer.toByteArray()),installed);assert new File(installed,"installed").isFile();assert Files.readAllBytes(new File(installed,"headers/test.h").toPath())[0]==42;
  buffer.reset();try(ZipOutputStream zip=new ZipOutputStream(buffer)){zip.putNextEntry(new ZipEntry("../escape"));zip.write(1);zip.closeEntry();}
  try{GameCompiler.install(new ByteArrayInputStream(buffer.toByteArray()),new File(root,"bad"));throw new AssertionError("zip traversal accepted");}catch(IOException expected){}
  assert !new File(root,"escape").exists();
  System.out.println("PASS: cache integrity, cancellation, failed build cleanup and compiler installation");
 }
}
