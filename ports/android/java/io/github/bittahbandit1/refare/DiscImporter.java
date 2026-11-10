package io.github.bittahbandit1.refare;
import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;

/** Bounded GameCube ISO/CISO reader. No executable compilation occurs on-device. */
public final class DiscImporter {
 static final String DOL_HASH="ca0e38f6b7d481fc512e4d67c8704261ce95a01ccc6a8680d963f6b8edd494d7";
 static final class Disc implements AutoCloseable {
  final RandomAccessFile file; final long physical;
  long logical,blockSize;long[] blocks;
  Disc(File source)throws Exception {
   file=new RandomAccessFile(source,"r");physical=file.length();logical=physical;
   try {
   byte[] signature=physical(0,8);
   if(signature[0]=='C'&&signature[1]=='I'&&signature[2]=='S'&&signature[3]=='O') {
    byte[] header=physical(0,0x8000);
    blockSize=(header[4]&255L)|((header[5]&255L)<<8)|((header[6]&255L)<<16)|((header[7]&255L)<<24);
    if(blockSize<0x440||blockSize>32*1024*1024)throw new IOException("Invalid GameCube CISO block size");
    blocks=new long[0x7ff8];long next=0x8000;
    for(int i=0;i<blocks.length;i++) {
     int present=header[i+8]&255;
     if(present>1)throw new IOException("Invalid CISO block map");
     blocks[i]=present==1?next:-1;if(present==1)next+=blockSize;
    }
    if(next>physical||blocks[0]<0)throw new IOException("Truncated GameCube CISO");
    logical=Math.min(1459978240L,blocks.length*blockSize);
   }
   } catch(Exception error) {file.close();throw error;}
  }
  byte[] physical(long offset,int count)throws IOException {
   if(offset<0||count<0||offset>physical||count>physical-offset)throw new IOException("Disc image is truncated");
   byte[] data=new byte[count];file.seek(offset);file.readFully(data);return data;
  }
  byte[] read(long offset,int count)throws IOException {
   if(offset<0||count<0||offset>logical||count>logical-offset)throw new IOException("Disc entry is out of bounds");
   if(blocks==null)return physical(offset,count);
   byte[] data=new byte[count];int cursor=0;
   while(cursor<count) {
    int block=(int)(offset/blockSize);long within=offset%blockSize;
    int amount=(int)Math.min(count-cursor,blockSize-within);
    if(blocks[block]>=0)System.arraycopy(physical(blocks[block]+within,amount),0,data,cursor,amount);
    offset+=amount;cursor+=amount;
   }
   return data;
  }
  void copy(long offset,long count,File output)throws IOException {
   if(offset<0||count<0||offset>logical||count>logical-offset)throw new IOException("Disc file lies outside the image");
   try(OutputStream stream=new BufferedOutputStream(new FileOutputStream(output))) {
    while(count>0){int n=(int)Math.min(count,1024*1024);stream.write(read(offset,n));offset+=n;count-=n;}
   }
  }
  public void close()throws IOException {file.close();}
 }
 static long word(byte[] data,int offset)throws IOException {
  if(offset<0||offset+4>data.length)throw new IOException("Truncated disc structure");
  return ((data[offset]&255L)<<24)|((data[offset+1]&255L)<<16)|((data[offset+2]&255L)<<8)|(data[offset+3]&255L);
 }
 static String hex(byte[] data)throws Exception {
  byte[] hash=MessageDigest.getInstance("SHA-256").digest(data);StringBuilder result=new StringBuilder();
  for(byte b:hash)result.append(String.format(Locale.ROOT,"%02x",b&255));return result.toString();
 }
 static final class Directory {final File path;final int index,end;Directory(File p,int i,int e){path=p;index=i;end=e;}}
 public static int extract(File source,File destination)throws Exception{return extract(source,destination,DOL_HASH);}
 static int extract(File source,File destination,String expectedHash)throws Exception {
  if(destination.exists())throw new IOException("This game is already imported. Choose Play.");
  File parent=destination.getParentFile();if(!parent.isDirectory()&&!parent.mkdirs())throw new IOException("Cannot create library folder");
  File stage=new File(parent,"import-"+UUID.randomUUID());if(!stage.mkdir())throw new IOException("Cannot stage game import");
  try(Disc disc=new Disc(source)) {
   byte[] boot=disc.read(0,0x440);
   if(!new String(boot,0,6,"US-ASCII").equals("GCTE51")||boot[7]!=0||word(boot,0x1c)!=0xc2339f3dL)
    throw new IOException("This build requires Crazy Taxi USA (GCTE51), revision 0");
   long dolOffset=word(boot,0x420),fstOffset=word(boot,0x424),fstSize=word(boot,0x428);
   if(fstSize<12||fstSize>16*1024*1024)throw new IOException("Invalid filesystem table size");
   byte[] header=disc.read(dolOffset,0x100);long dolSize=0x100;
   for(int i=0;i<18;i++){long size=word(header,0x90+4*i);if(size>0)dolSize=Math.max(dolSize,word(header,4*i)+size);}
   if(dolSize>64*1024*1024)throw new IOException("Invalid executable size");
   byte[] dol=disc.read(dolOffset,(int)dolSize);
   if(!hex(dol).equals(expectedHash))throw new IOException("Unsupported game executable revision");
   File sys=new File(stage,"sys"),files=new File(stage,"files");if(!sys.mkdir()||!files.mkdir())throw new IOException("Cannot create extraction folders");
   Files.write(new File(sys,"boot.bin").toPath(),boot);
   disc.copy(0x440,0x2000,new File(sys,"bi2.bin"));
   byte[] app=disc.read(0x2440,0x20);long appSize=0x20+word(app,0x14)+word(app,0x18);
   if(appSize>16*1024*1024)throw new IOException("Invalid apploader size");
   disc.copy(0x2440,appSize,new File(sys,"apploader.img"));
   Files.write(new File(sys,"main.dol").toPath(),dol);
   byte[] fst=disc.read(fstOffset,(int)fstSize);Files.write(new File(sys,"fst.bin").toPath(),fst);
   long number=word(fst,8);if(number<1||number>100000||number*12>fst.length||word(fst,0)>>>24!=1)throw new IOException("Invalid filesystem root");
   int count=(int)number,tableSize=count*12,imported=0;
   Deque<Directory> parents=new ArrayDeque<>();parents.push(new Directory(files,0,count));
   for(int i=1;i<count;i++) {
    while(parents.size()>1&&i>=parents.peek().end)parents.pop();
    Directory current=parents.peek();long typeName=word(fst,12*i);int type=(int)(typeName>>>24);
    int nameOffset=tableSize+(int)(typeName&0xffffff);if(nameOffset>=fst.length)throw new IOException("Invalid filesystem name offset");
    int end=nameOffset;while(end<fst.length&&fst[end]!=0)end++;
    if(end==fst.length)throw new IOException("Unterminated filesystem name");
    String name=new String(fst,nameOffset,end-nameOffset,"UTF-8");
    if(name.isEmpty()||name.equals(".")||name.equals("..")||name.contains("/")||name.contains("\\")||name.contains(":"))throw new IOException("Unsafe disc filename");
    File output=new File(current.path,name);
    if(output.exists())throw new IOException("Duplicate disc filename");
    if(type==1) {
     long stop=word(fst,12*i+8);
     if(word(fst,12*i+4)!=current.index||stop<=i||stop>current.end)throw new IOException("Invalid filesystem directory");
     if(!output.mkdir())throw new IOException("Cannot create disc directory");
     parents.push(new Directory(output,i,(int)stop));
    }else if(type==0){disc.copy(word(fst,12*i+4),word(fst,12*i+8),output);imported++;}
    else throw new IOException("Invalid filesystem entry type");
   }
   if(!stage.renameTo(destination))throw new IOException("Cannot finish game import");
   return imported;
  }finally{if(stage.exists())remove(stage);}
 }
 static void remove(File path){File[] children=path.listFiles();if(children!=null)for(File child:children)remove(child);path.delete();}
}
