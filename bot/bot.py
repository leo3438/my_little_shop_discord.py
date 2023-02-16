#! a faire  avant :
# pip install discord.py
# pip install paypalrestsdk


#import extern
import discord
from discord.ext import commands
import pymongo
import paypalrestsdk


#config db
db = client['votre_base_de_donnees']
collection = db['votre_collection']


#config bot
client = commands.Bot(command_prefix='!')
TOKEN = 'votre_token_de_bot'



#commands
@client.command()
async def ajouter_produit(ctx, nom_produit, prix):
    db = pymongo.MongoClient().bot_db
    produits = db.produits
    produits.insert_one({"nom_produit": nom_produit, "prix": prix})
    await ctx.send(f"Le produit {nom_produit} a été ajouté à la boutique au prix de {prix} $.")
@client.command()
async def ajouter_produit(ctx, nom_produit, prix):
    db = pymongo.MongoClient().bot_db
    produits = db.produits
    produits.insert_one({"nom_produit": nom_produit, "prix": prix})
    await ctx.send(f"Le produit {nom_produit} a été ajouté à la boutique au prix de {prix} $.")



#payement stripe
import stripe

stripe.api_key = "votre_clé_secrète_stripe"

customer = stripe.Customer.create(email="email@domain.com")
charge = stripe.Charge.create(
  customer=customer.id,
  amount=500,
  currency="usd",
  description="Example charge"
)

#payement paypal

def create_payment(price):
    paypalrestsdk.configure({
        "mode": "sandbox",  # Utiliser "live" pour les paiements en production
        "client_id": "votre_client_id_paypal",
        "client_secret": "votre_secret_paypal"
    })

    payment = paypalrestsdk.Payment({
        "intent": "sale",
        "payer": {
            "payment_method": "paypal"
        },
        "redirect_urls": {
            "return_url": "https://votre-site.com/execute-payment",
            "cancel_url": "https://votre-site.com/cancel-payment"
        },
        "transactions": [{
            "amount": {
                "total": price,
                "currency": "USD"
            },
            "description": "Achat dans la boutique Discord"
        }]
    })

    if payment.create():
        return payment.id
    else:
        return None