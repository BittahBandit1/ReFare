package io.github.bittahbandit1.refare;
import android.app.*;
import android.os.*;
import android.content.*;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.BitmapFactory;
import android.net.*;
import android.provider.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.net.*;
import java.util.*;

public class MainActivity extends Activity implements SurfaceHolder.Callback {
 static {System.loadLibrary("refare");}
 private static Context application;
 private Typeface uiFont;
 private static native void nativeStart(Surface surface,String game,String user,String sys,String module,float scale);
 private static native String nativePrepareGame(String game,String manifest);
 private static native void nativeStop();
 private static native void nativePause(boolean pause);
 private static native int nativeState();
 private static native String nativeMessage();
 private static native long nativeFrames();
 private static native boolean nativeMenuOpen();
 private static native void nativeInput(double[] values);
 private static native void nativeMenuButtons(int mask);
 private static native void nativePointer(float x,float y,boolean down);
 private static native void nativeToggleMenu();
 private static native void nativeResize();
 private final Handler handler=new Handler(Looper.getMainLooper());
 private final HashSet<Integer> keys=new HashSet<>();
 private final double[] axes=new double[18];
 private boolean playing=false,busy=false,alive=true,autoPlay=false;
 private TextView status,loading;
 private File library,user,sys,compilerRoot,compiledModule;
 private String compilerId;
 private GameCompiler compiler;
 private SurfaceView surface;
 private LinearLayout gameBar;
 private final Runnable hideBar=()->{if(gameBar!=null)gameBar.setVisibility(View.GONE);};
 private void revealBar(){if(gameBar!=null){gameBar.setVisibility(View.VISIBLE);handler.removeCallbacks(hideBar);handler.postDelayed(hideBar,3000);}}
 private String pendingDisc;
 private long previousFrames=-1;
 private final Runnable tick=new Runnable(){public void run(){
  if(!alive)return;
  if(playing){
   sendInput();int state=nativeState();long frames=nativeFrames();
   if(frames>0&&loading!=null)loading.setVisibility(View.GONE);
   if(nativeMenuOpen()&&gameBar!=null)gameBar.setVisibility(View.GONE);
   if(state>=2){playing=false;keys.clear();Arrays.fill(axes,0);home(nativeMessage()+" ("+frames+" frames)");}
   else if(frames/60!=previousFrames/60){previousFrames=frames;android.util.Log.i("ReFare","Frames="+frames+"; menu="+nativeMenuOpen());}
  }
  handler.postDelayed(this,16);
 }};
 @Override public void onCreate(Bundle saved){
  super.onCreate(saved);application=getApplicationContext();getExternalFilesDir(null);
  uiFont=Typeface.createFromAsset(getAssets(),"ReFareUI.ttf");
  setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
  getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
  immersive();library=new File(getFilesDir(),"GCTE51");user=new File(getFilesDir(),"user");sys=new File(getFilesDir(),"Sys");
  pendingDisc=getIntent().getStringExtra("disc");autoPlay=getIntent().getBooleanExtra("autoplay",false);
  home("Preparing runtime files…");busy=true;
  new Thread(()->{try{
   compilerId=readAsset("compiler-id.txt").trim();
   compilerRoot=new File(getFilesDir(),"compiler/"+compilerId);
   if(!new File(compilerRoot,"installed").isFile())GameCompiler.install(getAssets().open("compiler.zip"),compilerRoot);
   File marker=new File(getFilesDir(),"runtime-assets-"+compilerId);
   if(!marker.isFile()){
    copyAssets("Sys",sys);copyAsset("GFX.ini",new File(getFilesDir(),"GFX.ini"));copyAsset("startup-patches.csv",new File(getFilesDir(),"startup-patches.csv"));
    marker.createNewFile();
   }
   runOnUiThread(()->{if(!alive)return;busy=false;home(ready()?"Your disc is ready. The game will compile locally before first play.":"Choose your Crazy Taxi USA revision 0 ISO/CISO.");if(pendingDisc!=null){String disc=pendingDisc;pendingDisc=null;importFile(new File(disc));}else if(autoPlay&&ready())play();});
  }catch(Exception error){runOnUiThread(()->{if(!alive)return;busy=false;home(error.toString());});}},"ReFare assets").start();
  handler.post(tick);
 }
 @Override protected void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);String disc=intent.getStringExtra("disc");autoPlay=intent.getBooleanExtra("autoplay",false);if(disc!=null)importFile(new File(disc));else if(autoPlay&&ready())play();}
 private boolean ready(){return new File(library,"sys/main.dol").isFile();}
 @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);if(focused)immersive();}
 private void immersive(){getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY|View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_LAYOUT_STABLE|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);}
 private void home(String text){
  handler.removeCallbacks(hideBar);gameBar=null;
  float density=getResources().getDisplayMetrics().density;
  LinearLayout outer=new LinearLayout(this);outer.setOrientation(LinearLayout.VERTICAL);outer.setBackgroundColor(Color.rgb(255,204,32));
  FrameLayout banner=new FrameLayout(this);banner.setBackgroundColor(Color.WHITE);outer.addView(banner,new LinearLayout.LayoutParams(-1,(int)(160*density)));
  ImageView logo=new ImageView(this);try(InputStream image=getAssets().open("ReFareLogo.png")){logo.setImageBitmap(BitmapFactory.decodeStream(image));}catch(IOException error){android.util.Log.e("ReFare","Missing logo",error);}
  logo.setScaleType(ImageView.ScaleType.CENTER_CROP);logo.setContentDescription("ReFare");banner.addView(logo,new FrameLayout.LayoutParams((int)(280*density),(int)(140*density),Gravity.CENTER));
  LinearLayout view=new LinearLayout(this);view.setOrientation(LinearLayout.VERTICAL);view.setGravity(Gravity.TOP|Gravity.CENTER_HORIZONTAL);view.setPadding(32,24,32,24);outer.addView(view,new LinearLayout.LayoutParams(-1,-2));
  status=new TextView(this);status.setText(text);status.setTextColor(Color.BLACK);status.setGravity(Gravity.CENTER);status.setPadding(12,20,12,20);view.addView(status);
  Button choose=new Button(this);choose.setText("Choose ISO / CISO");choose.setEnabled(!busy);choose.setOnClickListener(v->{Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.setType("*/*");intent.addCategory(Intent.CATEGORY_OPENABLE);startActivityForResult(intent,10);});view.addView(choose);
  Button play=new Button(this);play.setText("Play Crazy Taxi");play.setEnabled(ready()&&!busy);play.setOnClickListener(v->play());view.addView(play);
  Button logs=new Button(this);logs.setText("View logs");logs.setOnClickListener(v->new AlertDialog.Builder(this).setItems(new String[]{"Game preparation log","Gameplay log"},(dialog,which)->showLog(new File(user,which==0?"preparation.log":"gameplay.log"))).show());view.addView(logs);
  if(compiler!=null){Button cancel=new Button(this);cancel.setText("Cancel preparation");cancel.setOnClickListener(v->{compiler.cancel();status.setText("Canceling game preparation…");cancel.setEnabled(false);});view.addView(cancel);}
  TextView hint=new TextView(this);hint.setText("ARM64 · Vulkan · L1: options · Start: pause\nGame code is generated locally from your own disc before play.");hint.setTextColor(Color.BLACK);hint.setGravity(Gravity.CENTER);hint.setPadding(0,12,0,0);view.addView(hint);
  applyInstallerFont(outer);ScrollView page=new ScrollView(this);page.setBackgroundColor(Color.rgb(255,204,32));page.setFillViewport(true);page.addView(outer);setContentView(page);immersive();
 }
 private void applyInstallerFont(View view){
  if(view instanceof TextView){TextView text=(TextView)view;text.setTypeface(uiFont);text.setTextSize(view instanceof Button?22:20);if(view instanceof Button){text.setTextColor(Color.WHITE);view.setBackgroundTintList(android.content.res.ColorStateList.valueOf(Color.rgb(168,17,0)));}}
  if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=0;i<group.getChildCount();i++)applyInstallerFont(group.getChildAt(i));}
 }
 private void copyAsset(String name,File destination)throws Exception{destination.getParentFile().mkdirs();try(InputStream input=getAssets().open(name);OutputStream out=new FileOutputStream(destination)){byte[] buffer=new byte[65536];int n;while((n=input.read(buffer))>=0)out.write(buffer,0,n);}}
 private void copyAssets(String path,File destination)throws Exception {String[] children=getAssets().list(path);if(children!=null&&children.length>0){destination.mkdirs();for(String child:children)copyAssets(path+"/"+child,new File(destination,child));}else copyAsset(path,destination);}
 @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==10&&result==RESULT_OK&&data!=null&&data.getData()!=null){Uri uri=data.getData();busy=true;home("Reading selected disc…");new Thread(()->{File source=new File(getCacheDir(),"disc-import.ciso");try(InputStream in=getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(source)){if(in==null)throw new IOException("The file provider could not open this disc");byte[] bytes=new byte[1024*1024];int n;while((n=in.read(bytes))>=0)out.write(bytes,0,n);runOnUiThread(()->{busy=false;importFile(source);});}catch(Exception e){source.delete();runOnUiThread(()->{busy=false;home(e.toString());});}},"ReFare disc copy").start();}}
 private void importFile(File source){
  if(busy||playing)return;
  if(ready()){if(autoPlay)play();else home("Game is already imported. Choose Play.");return;}
  busy=true;home("Importing and verifying disc…");
  new Thread(()->{try{int count=DiscImporter.extract(source,library);android.util.Log.i("ReFare","Imported "+count+" game files");if(source.getParentFile().equals(getCacheDir()))source.delete();runOnUiThread(()->{busy=false;home("Imported and verified "+count+" files.");if(autoPlay)play();});}catch(Exception e){android.util.Log.e("ReFare","Import failed",e);runOnUiThread(()->{busy=false;home(e.toString());});}},"ReFare disc importer").start();
 }
 private String readAsset(String name)throws Exception{try(InputStream in=getAssets().open(name);ByteArrayOutputStream bytes=new ByteArrayOutputStream()){byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))!=-1)bytes.write(buffer,0,n);return bytes.toString("UTF-8");}}
 private void showLog(File file){try(RandomAccessFile log=new RandomAccessFile(file,"r")){long size=log.length();log.seek(Math.max(0,size-60000));byte[] bytes=new byte[(int)Math.min(60000,size)];log.readFully(bytes);TextView text=new TextView(this);text.setText(new String(bytes,"UTF-8"));text.setTextIsSelectable(true);text.setPadding(24,16,24,16);ScrollView scroll=new ScrollView(this);scroll.addView(text);new AlertDialog.Builder(this).setTitle(file.getName()).setView(scroll).setPositiveButton("Close",null).show();}catch(Exception error){status.setText("No log available yet.");}}
 private void play(){
  if(!ready()||busy||playing||compilerRoot==null)return;
  busy=true;compiler=new GameCompiler(compilerRoot,new File(getApplicationInfo().nativeLibraryDir),new File(getFilesDir(),"modules"),new File(user,"preparation.log"),compilerId);
  GameCompiler task=compiler;home("Preparing game from your disc…");
  new Thread(()->{try{
   String error=nativePrepareGame(library.getAbsolutePath(),new File(getFilesDir(),"startup-patches.csv").getAbsolutePath());if(error!=null)throw new IOException(error);
   File module=task.prepare(new File(library,"sys/main.dol"),message->runOnUiThread(()->{if(alive&&compiler==task)status.setText(message);}));
   runOnUiThread(()->{if(!alive)return;compiler=null;busy=false;compiledModule=module;launchGame();});
  }catch(Exception error){android.util.Log.e("ReFare","Game preparation failed",error);runOnUiThread(()->{if(!alive)return;compiler=null;busy=false;home(error.getMessage());});}},"ReFare game preparation").start();
 }
 private void launchGame(){
  if(!alive||compiledModule==null)return;playing=true;previousFrames=-1;keys.clear();Arrays.fill(axes,0);
  FrameLayout root=new FrameLayout(this);surface=new SurfaceView(this);surface.getHolder().addCallback(this);root.addView(surface,new FrameLayout.LayoutParams(-1,-1));
  surface.setOnTouchListener((v,event)->{if(!nativeMenuOpen())revealBar();if(event.getPointerCount()==3&&event.getActionMasked()==MotionEvent.ACTION_POINTER_DOWN){nativeToggleMenu();return true;}if(nativeMenuOpen())nativePointer(event.getX()/v.getWidth(),event.getY()/v.getHeight(),event.getActionMasked()!=MotionEvent.ACTION_UP&&event.getActionMasked()!=MotionEvent.ACTION_CANCEL);return true;});
  loading=new TextView(this);loading.setText("Starting compiled game…");loading.setTextColor(Color.WHITE);loading.setTextSize(20);loading.setGravity(Gravity.CENTER);root.addView(loading,new FrameLayout.LayoutParams(-1,-1));
  LinearLayout top=new LinearLayout(this);Button options=new Button(this);options.setText("Options");options.setOnClickListener(v->nativeToggleMenu());top.addView(options);Button exit=new Button(this);exit.setText("Exit");exit.setOnClickListener(v->new AlertDialog.Builder(this).setMessage("Are you sure?").setNegativeButton("No",null).setPositiveButton("Yes",(dialog,which)->nativeStop()).show());top.addView(exit);root.addView(top,new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.LEFT));setContentView(root);gameBar=top;revealBar();immersive();
 }
 public void surfaceCreated(SurfaceHolder holder){nativeStart(holder.getSurface(),library.getAbsolutePath(),user.getAbsolutePath(),sys.getAbsolutePath(),compiledModule.getAbsolutePath(),getResources().getDisplayMetrics().density);}
 public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){nativeResize();}
 public void surfaceDestroyed(SurfaceHolder holder){if(playing)nativeStop();}
 @Override public void onBackPressed(){if(compiler!=null){compiler.cancel();return;}if(playing)nativeToggleMenu();else super.onBackPressed();}
 @Override protected void onPause(){super.onPause();if(playing){Arrays.fill(axes,0);keys.clear();sendInput();nativePause(true);}}
 @Override protected void onResume(){super.onResume();immersive();if(playing)nativePause(false);}
 @Override protected void onDestroy(){alive=false;handler.removeCallbacks(tick);if(compiler!=null)compiler.cancel();if(playing)nativeStop();super.onDestroy();}
 @Override public boolean dispatchKeyEvent(KeyEvent event){
  if(playing&&isGameKey(event.getKeyCode())){if(gameBar!=null)gameBar.setVisibility(View.GONE);if(event.getAction()==KeyEvent.ACTION_DOWN)keys.add(event.getKeyCode());else if(event.getAction()==KeyEvent.ACTION_UP)keys.remove(event.getKeyCode());sendInput();return true;}return super.dispatchKeyEvent(event);
 }
 private boolean isGameKey(int key){return (key>=KeyEvent.KEYCODE_BUTTON_A&&key<=KeyEvent.KEYCODE_BUTTON_MODE)||(key>=KeyEvent.KEYCODE_DPAD_UP&&key<=KeyEvent.KEYCODE_DPAD_CENTER);}
 private double axis(MotionEvent event,int axis){float value=event.getAxisValue(axis);return Math.abs(value)<0.15?0:value;}
 @Override public boolean onGenericMotionEvent(MotionEvent event){
  boolean joystick=event.isFromSource(InputDevice.SOURCE_JOYSTICK);
  boolean dpad=event.isFromSource(InputDevice.SOURCE_DPAD);
  if(playing&&(joystick||dpad)){
   if(gameBar!=null)gameBar.setVisibility(View.GONE);
   if(event.getActionMasked()==MotionEvent.ACTION_CANCEL){Arrays.fill(axes,0);sendInput();return true;}
   if(event.getActionMasked()!=MotionEvent.ACTION_MOVE)return super.onGenericMotionEvent(event);
   // Handheld controllers commonly report the D-pad as a hat, not key events.
   ControllerMapping.readHat(axes,event.getAxisValue(MotionEvent.AXIS_HAT_X),event.getAxisValue(MotionEvent.AXIS_HAT_Y));
   if(joystick){
    axes[14]=axis(event,MotionEvent.AXIS_X);axes[15]=-axis(event,MotionEvent.AXIS_Y);
    axes[16]=axis(event,MotionEvent.AXIS_Z);axes[17]=-axis(event,MotionEvent.AXIS_RZ);
    axes[12]=Math.max(0,Math.max(axis(event,MotionEvent.AXIS_LTRIGGER),axis(event,MotionEvent.AXIS_BRAKE)));
    axes[13]=Math.max(0,Math.max(axis(event,MotionEvent.AXIS_RTRIGGER),axis(event,MotionEvent.AXIS_GAS)));
   }
   sendInput();return true;
  }
  return super.onGenericMotionEvent(event);
 }
 private boolean held(int key){return keys.contains(key);}
 private void sendInput(){
  double[] values=axes.clone();int[] codes={96,97,99,100,103,108,19,20,21,22};
  for(int i=0;i<codes.length;i++)if(held(codes[i]))values[i]=1;
  if(held(KeyEvent.KEYCODE_DPAD_CENTER))values[0]=1;
  ControllerMapping.applyDpadToStick(values);
  if(held(104)){values[10]=1;values[12]=1;}if(held(105)){values[11]=1;values[13]=1;}
  if(values[12]>.9)values[10]=1;if(values[13]>.9)values[11]=1;
  int mask=0;if(held(102))mask|=1;if(values[6]>0||values[15]>.5)mask|=2;if(values[7]>0||values[15]<-.5)mask|=4;if(values[8]>0||values[14]<-.5)mask|=8;if(values[9]>0||values[14]>.5)mask|=16;if(values[1]>0)mask|=32;if(values[0]>0)mask|=64;
  nativeMenuButtons(mask);nativeInput(values);
 }
 // Native filesystem helpers. The importer opens SAF documents through ContentResolver.
 public static int openContent(String uri,String mode){try(ParcelFileDescriptor fd=application.getContentResolver().openFileDescriptor(Uri.parse(uri),mode)){return fd==null?-1:fd.detachFd();}catch(Exception e){return -1;}}
 public static boolean deleteContent(String uri){try{return DocumentsContract.deleteDocument(application.getContentResolver(),Uri.parse(uri));}catch(Exception e){return false;}}
 public static long contentSize(String uri){try(Cursor c=application.getContentResolver().query(Uri.parse(uri),new String[]{DocumentsContract.Document.COLUMN_MIME_TYPE,OpenableColumns.SIZE},null,null,null)){if(c!=null&&c.moveToFirst())return DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(0))?-2:c.getLong(1);}catch(Exception ignored){}return -1;}
 public static String contentName(String uri){try(Cursor c=application.getContentResolver().query(Uri.parse(uri),new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){if(c!=null&&c.moveToFirst())return c.getString(0);}catch(Exception ignored){}return "";}
 public static String[] childNames(String directory,boolean uriNames){ArrayList<String> names=new ArrayList<>();try{Uri tree=Uri.parse(directory);String id=DocumentsContract.isDocumentUri(application,tree)?DocumentsContract.getDocumentId(tree):DocumentsContract.getTreeDocumentId(tree);Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,id);try(Cursor c=application.getContentResolver().query(children,new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,DocumentsContract.Document.COLUMN_DISPLAY_NAME},null,null,null)){while(c!=null&&c.moveToNext())names.add(uriNames?DocumentsContract.buildDocumentUriUsingTree(tree,c.getString(0)).toString():c.getString(1));}}catch(Exception ignored){}return names.toArray(new String[0]);}
 public static String[] searchContent(String directory,String[] extensions,boolean recursive){ArrayList<String> result=new ArrayList<>();ArrayDeque<String> queue=new ArrayDeque<>();queue.add(directory);int scanned=0;while(!queue.isEmpty()&&scanned++<10000){for(String uri:childNames(queue.remove(),true)){if(contentSize(uri)==-2){if(recursive)queue.add(uri);}else{String name=contentName(uri).toLowerCase(Locale.ROOT);boolean matches=extensions.length==0;for(String ext:extensions)if(name.endsWith(ext.toLowerCase(Locale.ROOT)))matches=true;if(matches)result.add(uri);}}}return result.toArray(new String[0]);}
 private static LinkProperties network(){ConnectivityManager m=(ConnectivityManager)application.getSystemService(CONNECTIVITY_SERVICE);return m==null?null:m.getLinkProperties(m.getActiveNetwork());}
 private static int address(InetAddress a){byte[] b=a.getAddress();if(b.length!=4)return 0;return (b[0]&255)|((b[1]&255)<<8)|((b[2]&255)<<16)|((b[3]&255)<<24);}
 public static int networkAddress(){try{LinkProperties p=network();if(p!=null)for(LinkAddress a:p.getLinkAddresses())if(a.getAddress() instanceof Inet4Address)return address(a.getAddress());}catch(Exception ignored){}return 0;}
 public static int networkPrefix(){try{LinkProperties p=network();if(p!=null)for(LinkAddress a:p.getLinkAddresses())if(a.getAddress() instanceof Inet4Address)return a.getPrefixLength();}catch(Exception ignored){}return 0;}
 public static int networkGateway(){try{LinkProperties p=network();if(p!=null)for(RouteInfo route:p.getRoutes())if(route.isDefaultRoute()&&route.getGateway() instanceof Inet4Address)return address(route.getGateway());}catch(Exception ignored){}return 0;}
}
