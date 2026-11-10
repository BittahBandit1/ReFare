package io.github.bittahbandit1.refare;
import java.io.File;
public final class AndroidImportTest {
 public static void main(String[] args)throws Exception {
  int count=args.length==3?DiscImporter.extract(new File(args[0]),new File(args[1]),args[2]):DiscImporter.extract(new File(args[0]),new File(args[1]));
  System.out.println("Imported "+count+" game files");
 }
}
