package jfcontrols.logic;

/** Examine On.
 *
 * @author pquiring
 */

import javaforce.controls.*;

public class COIL extends Logic {

  public boolean isBlock() {
    return false;
  }

  public String getName() {
    return "Coil";
  }

  public String getCode() {
    return "setBoolean(tags[1], enabled);\r\n";
  }

  public int getTagsCount() {
    return 1;
  }

  public int getTagType(int idx) {
    return TagType.bit;
  }
}
