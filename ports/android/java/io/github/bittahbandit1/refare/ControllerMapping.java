package io.github.bittahbandit1.refare;

/** Shared conversion for Android hat axes and GameCube directional controls. */
final class ControllerMapping {
 static void readHat(double[] controls,float x,float y) {
  controls[6]=y<-.5f?1:0;
  controls[7]=y>.5f?1:0;
  controls[8]=x<-.5f?1:0;
  controls[9]=x>.5f?1:0;
 }
 static void applyDpadToStick(double[] controls) {
  // Crazy Taxi navigates using the main stick. Preserve an active analog stick.
  if(controls[14]==0)controls[14]=controls[9]-controls[8];
  if(controls[15]==0)controls[15]=controls[6]-controls[7];
 }
}
