package io.github.bittahbandit1.refare;

import java.io.*;
import java.nio.file.Files;
import java.util.*;
import java.util.regex.*;

/** Creates the chassis coverage/hash tables from this user's DolRecomp output. */
final class ModuleTables {
 private static long word(byte[] bytes,int offset)throws IOException {
  if(offset<0||offset+4>bytes.length)throw new IOException("Truncated DOL header");
  return ((bytes[offset]&255L)<<24)|((bytes[offset+1]&255L)<<16)|((bytes[offset+2]&255L)<<8)|(bytes[offset+3]&255L);
 }
 private static long hex(String text){return Long.parseLong(text.replaceFirst("^0x",""),16);}
 private static void ranges(StringBuilder out,String name,String count,List<long[]> values){
  out.append("static const StaticRecompRange ").append(name).append("[] = {\n");
  if(values.isEmpty())out.append("    {0u, 0u},\n");
  for(long[] range:values)out.append(String.format(Locale.ROOT,"    {0x%08Xu, 0x%08Xu},\n",range[0],range[1]));
  out.append("};\n#define ").append(count).append(' ').append(values.size()).append("u\n");
 }
 static void write(File generated,File output)throws IOException {
  String header=new String(Files.readAllBytes(new File(generated,"generated.h").toPath()),"UTF-8");
  TreeMap<Long,Long> code=new TreeMap<>();
  Matcher matcher=Pattern.compile("address >= (0x[0-9A-Fa-f]+)u && address < (0x[0-9A-Fa-f]+)u").matcher(header);
  while(matcher.find())code.put(hex(matcher.group(1)),hex(matcher.group(2)));
  matcher=Pattern.compile("u32\\s+offset\\s*=\\s*address\\s*-\\s*(0x[0-9A-Fa-f]+)u\\s*;\\s*if\\s*\\(\\s*offset\\s*<\\s*(0x[0-9A-Fa-f]+)u").matcher(header);
  while(matcher.find()){long start=hex(matcher.group(1));code.put(start,start+hex(matcher.group(2)));}
  TreeSet<Long> starts=new TreeSet<>();matcher=Pattern.compile("void func_([0-9A-Fa-f]{8})\\(CPUState\\* ctx\\);").matcher(header);
  while(matcher.find())starts.add(hex(matcher.group(1)));
  if(code.isEmpty()||starts.isEmpty())throw new IOException("Recompiler produced no executable coverage");
  List<long[]> coverage=new ArrayList<>(),chunks=new ArrayList<>(),smc=new ArrayList<>();
  for(Map.Entry<Long,Long> range:code.entrySet())coverage.add(new long[]{range.getKey(),range.getValue()});
  List<Long> addresses=new ArrayList<>(starts);
  for(int i=0;i<addresses.size();i++){
   long start=addresses.get(i);Map.Entry<Long,Long> containing=code.floorEntry(start);
   if(containing==null||start>=containing.getValue())throw new IOException("Chunk outside executable coverage");
   long end=containing.getValue();if(i+1<addresses.size()&&addresses.get(i+1)<end)end=addresses.get(i+1);
   chunks.add(new long[]{start,end});
  }
  for(String line:Files.readAllLines(new File(generated,"generated_smc.txt").toPath())){
   matcher=Pattern.compile("^(0x[0-9A-Fa-f]+)-(0x[0-9A-Fa-f]+)").matcher(line.trim());
   if(matcher.find())smc.add(new long[]{hex(matcher.group(1)),hex(matcher.group(2))+4});
  }
  Collections.sort(smc,(a,b)->Long.compare(a[0],b[0]));
  byte[] dol=Files.readAllBytes(new File(generated,"main.dol").toPath());
  if(dol.length<0x100)throw new IOException("Truncated generated DOL");
  StringBuilder out=new StringBuilder("// Generated locally from the imported disc.\n");
  ranges(out,"s_code_ranges","MODULE_CODE_RANGE_COUNT",coverage);ranges(out,"s_smc_ranges","MODULE_SMC_RANGE_COUNT",smc);ranges(out,"s_chunk_ranges","MODULE_CHUNK_RANGE_COUNT",chunks);
  out.append("static const u64 s_chunk_hashes[] = {\n");
  for(long[] chunk:chunks){
   long offset=-1;
   for(int i=0;i<18;i++){long file=word(dol,i*4),base=word(dol,0x48+i*4),size=word(dol,0x90+i*4);if(file!=0&&base<=chunk[0]&&chunk[1]<=base+size){offset=file+chunk[0]-base;break;}}
   long length=chunk[1]-chunk[0];if(offset<0||offset>dol.length||length>dol.length-offset)throw new IOException("Invalid executable chunk range");
   long hash=0xcbf29ce484222325L;for(long j=0;j<length;j++)hash=(hash^(dol[(int)(offset+j)]&255L))*0x100000001b3L;
   out.append(String.format(Locale.ROOT,"    0x%016Xu,\n",hash));
  }
  out.append("};\n");Files.write(output.toPath(),out.toString().getBytes("UTF-8"));
 }
}
