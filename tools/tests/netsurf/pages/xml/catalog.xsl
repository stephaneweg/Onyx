<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="html"/>
<xsl:variable name="dear" select="10"/>
<xsl:template match="/">
<html>
<head>
<title>My CD Collection</title>
<style>
table { border-collapse: collapse; }
th { background: #9acd32; text-align: left; }
td, th { border: 1px solid #000; padding: 2px 6px; }
tr.dear td { color: #c00000; }
</style>
</head>
<body>
  <h2>My CD Collection (<xsl:value-of select="count(catalog/cd)"/>)</h2>
  <table id="cds">
    <tr><th>Title</th><th>Artist</th><th>Price</th></tr>
    <xsl:apply-templates select="catalog/cd">
      <xsl:sort select="artist"/>
    </xsl:apply-templates>
  </table>
  <p id="countries">
    <xsl:for-each select="catalog/cd[not(country = preceding-sibling::cd/country)]">
      <xsl:value-of select="country"/>
      <xsl:if test="position() != last()">, </xsl:if>
    </xsl:for-each>
  </p>
  <script>console.log("XMLT xslt page script " + document.querySelectorAll("#cds tr").length + " " + document.contentType);</script>
</body>
</html>
</xsl:template>
<xsl:template match="cd">
  <tr>
    <xsl:if test="price &gt; $dear"><xsl:attribute name="class">dear</xsl:attribute></xsl:if>
    <td><xsl:value-of select="title"/></td>
    <td><xsl:value-of select="artist"/></td>
    <td><xsl:value-of select="price"/></td>
  </tr>
</xsl:template>
</xsl:stylesheet>
